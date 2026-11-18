/**
 * SW Lithophane's CPU half, part one: the plate's physics and the controls,
 * ported by hand from source/Plate.{h,cpp} and source/Controls.{h,cpp}.
 *
 * **Only a reader checks this file against the C++**, and the crosscheck in
 * demo/tools/crosscheck/ (the page's pictures against lptest --pipe, and this
 * file's tables, kernels and plans against the C++ compiled into a printer).
 * Nothing else does. It is, section by section:
 *
 *   - Plate.h: every constant, with the same values.
 *   - Plate.cpp: the four materials (Sugiyama et al.'s three porcelain bodies
 *     and White PLA), the multipole's parameters (DiffusionOf), the slab's
 *     radial profile (Profile) and its per-axis variance in closed form
 *     (AxisVariance), the 15-Gaussian fit by non-negative least squares on
 *     111 rows with the total and the variance held (fit, Nnls, solve), the
 *     sampled kernels with their radius fixed first and sigma bisected
 *     (ExactKernel), and the frame's plan (PlanFor: the active scales, the
 *     delta column, each scale's factor, the groups of up to four, the weight
 *     table as uploaded and the kernels).
 *   - Controls.h / Controls.cpp: the table of controls (names, groups, kinds,
 *     element lists, defaults as the host's floats) and every conversion
 *     from the host's 0..1 to millimetres, degrees and light levels.
 *
 * All in double, as the C++ is (a JS number is a double); a value the C++
 * casts to float goes through Math.fround. The build of the plugin fuses
 * multiply-adds (Apple clang's default) and V8's exp and pow are not Apple's
 * libm, so a double here can differ from the plugin's in its last bits; what
 * the GPU is handed is float, and the crosscheck compares those.
 *
 * Not ported: the harness's negative controls (PlanFor's kernelModel 1 and 2:
 * the page passes 0, as the plugin does), FrontReflectance (the harness's
 * report of the K-M reflectance: the shader computes its own), and the log.
 *
 * No DOM and no GL here, so a module Worker (demo/fit-worker.js) and node can
 * import it.
 */

/** static_cast< float >: what a host's float, or a glUniform1f, keeps of a double. */
export const f32 = Math.fround;

/** std::clamp, for numbers. */
const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);

// ==== PORT: Plate.h ==========================================================

export const kScales = 15;
export const kScale0 = 0.0625; // mm, sigma_0
export const kRowOrigin = 0.5; // mm, the table's first row
export const kRowStep = 0.05; // mm
export const kRows = 111; // 0.5 .. 6.0 mm
export const kDeltaColumn = kScales; // the weight table's column for the scales under a quarter pixel
export const kTableWidth = kScales + 1;
export const kMaxGroupScales = 4;
export const kMaxGroups = 8;
export const kDeltaBelowPx = 0.25; // a scale narrower than this is the pixel itself
export const kActiveWeight = 1e-5; // a scale weighing less everywhere in the range is dropped
export const kImagePairs = 30;

// ==== PORT: Plate.cpp ========================================================

const kPi = 3.14159265358979323846;
/** The weight of the fit's two constraint rows (total, variance). Chosen in the C++. */
const kHold = 100.0;

/** Donner & Jensen's polynomial fit of the diffuse Fresnel reflectance, for eta > 1. */
function diffuseFresnel(eta) {
  return -1.4399 / (eta * eta) + 0.7099 / eta + 0.6681 + 0.0636 * eta;
}

/** sigma_a from the measured apparent coefficient k: sqrt( 3 mua ( mua + musp ) ) = k. */
function absorptionFromApparent(k, musp) {
  return 0.5 * (-musp + Math.sqrt(musp * musp + (4.0 * k * k) / 3.0));
}

function makePla() {
  // Wang & Zhou 2023's two-point slope; Ragunathan et al.'s reduced
  // scattering extrapolated (chosen); K = 2 mua, S = 3/4 musp (Pham et al.).
  const m = { name: 'White PLA', k: [0.293, 0.331, 0.429], K: [0, 0, 0], S: [0, 0, 0], musp: [3.75, 3.75, 3.75], Fdr: diffuseFresnel(1.4) };
  for (let c = 0; c < 3; c += 1) {
    m.K[c] = 2.0 * absorptionFromApparent(m.k[c], m.musp[c]);
    m.S[c] = 0.75 * m.musp[c];
  }
  return m;
}

function porcelain(name, k, K, S) {
  // musp = 4 S / 3 (Pham et al.); F_dr = 0.5 (Sugiyama, after Hogg).
  const m = { name, k: [k[0], k[1], k[2]], K: [K[0], K[1], K[2]], S: [S[0], S[1], S[2]], musp: [0, 0, 0], Fdr: 0.5 };
  for (let c = 0; c < 3; c += 1) m.musp[c] = (4.0 * S[c]) / 3.0;
  return m;
}

// Sugiyama et al. 1996: k from Fig. 5, K from Fig. 6, S from Fig. 7, at
// 650 / 550 / 450 nm.
const MATERIALS = Object.freeze([
  porcelain('Porcelain', [0.90, 1.01, 1.42], [0.064, 0.064, 0.101], [3.74, 4.73, 6.43]),
  porcelain('Feldspar Porcelain', [0.58, 0.59, 0.75], [0.022, 0.011, 0.012], [2.80, 3.40, 4.35]),
  porcelain('Clay Porcelain', [1.49, 2.05, 2.95], [0.193, 0.300, 0.481], [4.74, 6.00, 8.51]),
  makePla(),
]);

/** Solve A x = b by Gaussian elimination with partial pivoting (n small). Returns x, or null. */
function solve(Ain, bin, n) {
  const A = Ain.slice();
  const b = bin.slice();
  for (let col = 0; col < n; col += 1) {
    let pivot = col;
    for (let r = col + 1; r < n; r += 1) if (Math.abs(A[r * n + col]) > Math.abs(A[pivot * n + col])) pivot = r;
    if (A[pivot * n + col] === 0.0) return null;
    if (pivot !== col) {
      for (let k = 0; k < n; k += 1) {
        const t = A[col * n + k];
        A[col * n + k] = A[pivot * n + k];
        A[pivot * n + k] = t;
      }
      const t = b[col];
      b[col] = b[pivot];
      b[pivot] = t;
    }
    for (let r = col + 1; r < n; r += 1) {
      const f = A[r * n + col] / A[col * n + col];
      for (let k = col; k < n; k += 1) A[r * n + k] -= f * A[col * n + k];
      b[r] -= f * b[col];
    }
  }
  const x = new Array(n).fill(0.0);
  for (let r = n - 1; r >= 0; r -= 1) {
    let s = b[r];
    for (let k = r + 1; k < n; k += 1) s -= A[r * n + k] * x[k];
    x[r] = s / A[r * n + r];
  }
  return x;
}

/**
 * One material's fitted table: weights[ (row * kScales + j) * 3 + c ], each
 * row and channel summing to 1, and the fit's worst 2-D L1 distance.
 * `progress(done, total)` is the page's, called once a row.
 */
function fit(m, progress) {
  // The radial grid: log-spaced, 1 um to 100 mm, the weights 2 pi r dr with
  // dr = r du.
  const N = 500;
  const u0 = Math.log(1e-3);
  const u1 = Math.log(100.0);
  const du = (u1 - u0) / (N - 1);
  const r = new Float64Array(N);
  const W = new Float64Array(N);
  for (let i = 0; i < N; i += 1) {
    r[i] = Math.exp(u0 + i * du);
    W[i] = 2.0 * kPi * r[i] * r[i] * du * (i === 0 || i === N - 1 ? 0.5 : 1.0);
  }
  const B = new Float64Array(N * kScales);
  for (let j = 0; j < kScales; j += 1) {
    const s = ScaleSigmaMm(j);
    for (let i = 0; i < N; i += 1) B[i * kScales + j] = Math.exp((-0.5 * r[i] * r[i]) / (s * s)) / (2.0 * kPi * s * s);
  }
  const G = new Array(kScales * kScales).fill(0.0);
  for (let a = 0; a < kScales; a += 1) {
    for (let b = 0; b < kScales; b += 1) {
      let s = 0.0;
      for (let i = 0; i < N; i += 1) s += W[i] * B[i * kScales + a] * B[i * kScales + b];
      G[a * kScales + b] = s;
    }
  }

  const table = { weights: new Float64Array(kRows * kScales * 3), worstL1: 0.0, worstL1At: [0, 0] };
  const T = new Float64Array(N);
  const h = new Array(kScales).fill(0.0);
  for (let c = 0; c < 3; c += 1) {
    const p = DiffusionOf(m, c);
    for (let row = 0; row < kRows; row += 1) {
      const d = kRowOrigin + kRowStep * row;
      let total = 0.0;
      for (let i = 0; i < N; i += 1) {
        T[i] = Profile(p, d, r[i]);
        total += W[i] * T[i];
      }
      for (let i = 0; i < N; i += 1) T[i] /= total;
      for (let j = 0; j < kScales; j += 1) {
        let s = 0.0;
        for (let i = 0; i < N; i += 1) s += W[i] * B[i * kScales + j] * T[i];
        h[j] = s;
      }
      // The total and the per-axis variance held to the profile's: two rows
      // added to the normal equations with weight kHold.
      const V = AxisVariance(p, d);
      const Gc = G.slice();
      const hc = h.slice();
      for (let a = 0; a < kScales; a += 1) {
        const ca = (ScaleSigmaMm(a) * ScaleSigmaMm(a)) / V;
        for (let b = 0; b < kScales; b += 1) Gc[a * kScales + b] += kHold * ((ca * ScaleSigmaMm(b) * ScaleSigmaMm(b)) / V + 1.0);
        hc[a] += kHold * (ca + 1.0);
      }
      let w = Nnls(Gc, hc, kScales);
      let sum = 0.0;
      for (const v of w) sum += v;
      if (!(sum > 0.0)) {
        w = new Array(kScales).fill(0.0);
        w[0] = sum = 1.0;
      }
      for (let j = 0; j < kScales; j += 1) w[j] /= sum;
      let l1 = 0.0;
      for (let i = 0; i < N; i += 1) {
        let f = 0.0;
        for (let j = 0; j < kScales; j += 1) f += w[j] * B[i * kScales + j];
        l1 += W[i] * Math.abs(f - T[i]);
      }
      if (l1 > table.worstL1) {
        table.worstL1 = l1;
        table.worstL1At[0] = d;
        table.worstL1At[1] = c;
      }
      for (let j = 0; j < kScales; j += 1) table.weights[(row * kScales + j) * 3 + c] = w[j];
      if (progress) progress(c * kRows + row + 1, 3 * kRows);
    }
  }
  return table;
}

export function ScaleSigmaMm(j) {
  return kScale0 * Math.pow(2.0, 0.5 * j);
}

export function MaterialAt(index) {
  return MATERIALS[clamp(index, 0, 3)];
}

/** The multipole's parameters for one channel. */
export function DiffusionOf(m, c) {
  const p = {};
  p.musp = m.musp[c];
  p.mua = absorptionFromApparent(m.k[c], p.musp);
  const t = p.mua + p.musp;
  const D = 1.0 / (3.0 * t);
  const A = (1.0 + m.Fdr) / (1.0 - m.Fdr);
  p.zb = 2.0 * A * D;
  p.ell = 1.0 / t;
  p.alpha = p.musp / t;
  p.sigtr = Math.sqrt(3.0 * p.mua * t);
  return p;
}

/** The slab's radial transmittance profile T(r), 1/mm^2 (Donner & Jensen eq. 13), kImagePairs each side. */
export function Profile(p, d, r) {
  let v = 0.0;
  for (let i = -kImagePairs; i <= kImagePairs; i += 1) {
    const zr = 2.0 * i * (d + 2.0 * p.zb) + p.ell;
    const zv = 2.0 * i * (d + 2.0 * p.zb) - p.ell - 2.0 * p.zb;
    for (let s = 0; s < 2; s += 1) {
      const h = d - (s === 0 ? zr : zv);
      const rho = Math.sqrt(r * r + h * h);
      const t = (h * (1.0 + p.sigtr * rho) * Math.exp(-p.sigtr * rho)) / (4.0 * kPi * rho * rho * rho);
      v += (s === 0 ? 1.0 : -1.0) * p.alpha * t;
    }
  }
  return v;
}

/** The normalised profile's per-axis variance, mm^2, in closed form from the same image sources. */
export function AxisVariance(p, d) {
  let M0 = 0.0;
  let M2 = 0.0;
  for (let i = -kImagePairs; i <= kImagePairs; i += 1) {
    const zr = 2.0 * i * (d + 2.0 * p.zb) + p.ell;
    const zv = 2.0 * i * (d + 2.0 * p.zb) - p.ell - 2.0 * p.zb;
    for (let s = 0; s < 2; s += 1) {
      const h = d - (s === 0 ? zr : zv);
      const sg = s === 0 ? 1.0 : -1.0;
      const e = Math.exp(-p.sigtr * Math.abs(h));
      M0 += sg * p.alpha * 0.5 * (h > 0.0 ? 1.0 : -1.0) * e;
      M2 += (sg * p.alpha * h * e) / p.sigtr;
    }
  }
  return M2 / (2.0 * M0);
}

/**
 * TableFor: the plugin fits a material once per process, the first time it
 * is asked for. The page does the same, and lets the fit be done elsewhere
 * (a Worker) and handed in with SetTable, because a fit takes the main
 * thread long enough to stall the page.
 */
const tables = [null, null, null, null];
export function TableFor(material, progress) {
  const m = clamp(material, 0, 3);
  if (!tables[m]) tables[m] = fit(MaterialAt(m), progress);
  return tables[m];
}
export function HasTable(material) {
  return tables[clamp(material, 0, 3)] !== null;
}
/** A table fitted by this same code in a Worker, handed back. */
export function SetTable(material, table) {
  tables[clamp(material, 0, 3)] = table;
}

/** Lawson-Hanson non-negative least squares on the normal equations G w = h (n x n, row-major). Returns w >= 0. */
export function Nnls(G, h, n) {
  let w = new Array(n).fill(0.0);
  let z = [];
  const passive = new Array(n).fill(false);
  let scale = 0.0;
  for (const v of h) scale = Math.max(scale, Math.abs(v));
  const tol = 1e-12 * Math.max(scale, 1e-300);
  for (let outer = 0; outer < 3 * n; outer += 1) {
    let t = -1;
    let best = tol;
    for (let j = 0; j < n; j += 1) {
      if (passive[j]) continue;
      let g = h[j];
      for (let k = 0; k < n; k += 1) g -= G[j * n + k] * w[k];
      if (g > best) {
        best = g;
        t = j;
      }
    }
    if (t < 0) break;
    passive[t] = true;
    for (let inner = 0; inner < 3 * n; inner += 1) {
      const P = [];
      for (let j = 0; j < n; j += 1) if (passive[j]) P.push(j);
      const np = P.length;
      const A = new Array(np * np);
      const b = new Array(np);
      for (let a = 0; a < np; a += 1) {
        b[a] = h[P[a]];
        for (let c = 0; c < np; c += 1) A[a * np + c] = G[P[a] * n + P[c]];
      }
      const x = solve(A, b, np);
      if (x === null) {
        passive[t] = false;
        break;
      }
      z = new Array(n).fill(0.0);
      let positive = true;
      for (let a = 0; a < np; a += 1) {
        z[P[a]] = x[a];
        positive = positive && x[a] > 0.0;
      }
      if (positive) {
        w = z.slice();
        break;
      }
      let alpha = 1.0;
      for (const j of P) {
        if (z[j] <= 0.0) {
          const q = w[j] / (w[j] - z[j]);
          alpha = q < alpha ? q : alpha; // std::min( alpha, q )
        }
      }
      for (let j = 0; j < n; j += 1) w[j] += alpha * (z[j] - w[j]);
      for (const j of P) {
        if (w[j] <= 1e-15) {
          passive[j] = false;
          w[j] = 0.0;
        }
      }
    }
  }
  return w;
}

/**
 * A sampled, truncated, normalised Gaussian whose discrete variance is
 * exactly `variance` (px^2): radius ceil( 4 sqrt( variance ) ), its width
 * solved by bisection.
 */
export function ExactKernel(variance) {
  if (!(variance > 0.0)) return [1.0];
  const R = Math.max(1, Math.trunc(Math.ceil(4.0 * Math.sqrt(variance))));
  const varianceOf = (s, out) => {
    let sum = 0.0;
    let second = 0.0;
    const g = new Array(2 * R + 1);
    for (let i = -R; i <= R; i += 1) {
      g[i + R] = Math.exp((-0.5 * (i * i)) / (s * s));
      sum += g[i + R];
      second += i * i * g[i + R];
    }
    if (out) {
      for (let k = 0; k < g.length; k += 1) g[k] /= sum;
      out.g = g;
    }
    return second / sum;
  };
  let lo = 1e-3;
  let hi = 4.0 * Math.sqrt(variance) + 2.0;
  for (let i = 0; i < 200 && hi - lo > 1e-15 * hi; i += 1) {
    const mid = 0.5 * (lo + hi);
    if (varianceOf(mid, null) < variance) lo = mid;
    else hi = mid;
  }
  const out = {};
  varianceOf(0.5 * (lo + hi), out);
  return out.g;
}

/**
 * The frame's plan. kernelModel is the harness's negative control; the page,
 * like the plugin, passes 0, and only that path is ported.
 */
export function PlanFor(material, dThin, dThick, plateWidthMm, width, height, kernelModel = 0) {
  if (kernelModel !== 0) throw new Error('the negative controls are not ported');
  const top = kRowOrigin + kRowStep * (kRows - 1);
  const plan = {
    pitch: 0.0,
    dThin: 0.0,
    dThick: 0.0,
    material: 0,
    groups: [],
    active: new Array(kScales).fill(false),
    delta: new Array(kScales).fill(false),
    factor: new Array(kScales).fill(0),
    sigmaPx: new Array(kScales).fill(0.0),
    table: null,
    kernels: null,
    kernelRadius: 0,
    kernelRows: 0,
  };
  plan.material = clamp(material, 0, 3);
  plan.dThin = clamp(dThin, kRowOrigin, top);
  plan.dThick = clamp(Math.max(dThick, dThin), plan.dThin, top);
  plan.pitch = plateWidthMm / Math.max(width, 1);
  const fitted = TableFor(plan.material);

  // The rows the frame can read (interpolation reads floor and floor + 1).
  const r0 = clamp(Math.floor((plan.dThin - kRowOrigin) / kRowStep), 0, kRows - 1);
  const r1 = clamp(Math.floor((plan.dThick - kRowOrigin) / kRowStep) + 1, 0, kRows - 1);

  const weightAt = (row, j, c) => fitted.weights[(row * kScales + j) * 3 + c];

  for (let j = 0; j < kScales; j += 1) {
    plan.sigmaPx[j] = ScaleSigmaMm(j) / plan.pitch;
    let most = 0.0;
    for (let row = r0; row <= r1; row += 1) for (let c = 0; c < 3; c += 1) most = Math.max(most, weightAt(row, j, c));
    plan.active[j] = most >= kActiveWeight;
    plan.delta[j] = plan.active[j] && plan.sigmaPx[j] < kDeltaBelowPx;
    let f = 1;
    if (plan.sigmaPx[j] >= 4.0) f = 1 << Math.trunc(Math.floor(Math.log2(plan.sigmaPx[j] / 2.0)));
    plan.factor[j] = f;
  }

  // The table as uploaded.
  plan.table = new Float32Array(kTableWidth * kRows * 4);
  for (let row = 0; row < kRows; row += 1) {
    for (let c = 0; c < 3; c += 1) {
      let kept = 0.0;
      for (let j = 0; j < kScales; j += 1) if (plan.active[j]) kept += weightAt(row, j, c);
      for (let j = 0; j < kScales; j += 1) {
        if (!plan.active[j] || !(kept > 0.0)) continue;
        const column = plan.delta[j] ? kDeltaColumn : j;
        plan.table[(row * kTableWidth + column) * 4 + c] += f32(weightAt(row, j, c) / kept);
      }
      if (!(kept > 0.0)) plan.table[(row * kTableWidth + kDeltaColumn) * 4 + c] = 1.0;
    }
  }

  // The groups, by factor, four scales at most each, and their kernels.
  const newGroup = () => ({ factor: 1, count: 0, scale: [0, 0, 0, 0], radius: [0, 0, 0, 0], kernelRow: [0, 0, 0, 0], lowWidth: 0, lowHeight: 0 });
  const kernels = [];
  for (let f = 1; f <= 1 << 20; f *= 2) {
    let g = newGroup();
    for (let j = 0; j < kScales; j += 1) {
      if (!plan.active[j] || plan.delta[j] || plan.factor[j] !== f) continue;
      if (g.count === kMaxGroupScales) {
        plan.groups.push(g);
        g = newGroup();
      }
      g.factor = f;
      const variance = f === 1 ? plan.sigmaPx[j] * plan.sigmaPx[j] : (plan.sigmaPx[j] * plan.sigmaPx[j]) / (f * f) - 0.5;
      kernels.push(ExactKernel(variance));
      g.scale[g.count] = j;
      g.radius[g.count] = Math.trunc(kernels[kernels.length - 1].length / 2);
      g.kernelRow[g.count] = kernels.length - 1;
      g.count += 1;
    }
    if (g.count > 0) plan.groups.push(g);
    if (f > 4 * Math.max(width, height)) break;
  }
  for (const g of plan.groups) {
    g.lowWidth = g.factor === 1 ? width : Math.trunc((width + g.factor - 1) / g.factor) + 4;
    g.lowHeight = g.factor === 1 ? height : Math.trunc((height + g.factor - 1) / g.factor) + 4;
  }

  plan.kernelRadius = 0;
  for (const k of kernels) plan.kernelRadius = Math.max(plan.kernelRadius, Math.trunc(k.length / 2));
  plan.kernelRows = Math.max(1, kernels.length);
  const stride = 2 * plan.kernelRadius + 1;
  plan.kernels = new Float32Array(stride * plan.kernelRows * 4);
  for (let k = 0; k < kernels.length; k += 1) {
    const R = Math.trunc(kernels[k].length / 2);
    for (let i = -R; i <= R; i += 1) plan.kernels[(k * stride + (i + plan.kernelRadius)) * 4] = f32(kernels[k][i + R]);
  }
  return plan;
}

// ==== PORT: Controls.h and Controls.cpp ======================================

/** ParamId: the host's indices. The About block (PT_ABOUT_TEXT on) is not here. */
export const PT = Object.freeze({
  THINNEST: 0,
  THICKEST: 1,
  MATERIAL: 2,
  MAPPING: 3,
  RELIEF_SIDE: 4,
  PLATE_WIDTH: 5,
  BACK_LIGHT: 6,
  FRONT_LIGHT: 7,
  LIGHT_ANGLE: 8,
  LIGHT_ELEVATION: 9,
  MIX: 10,
  ABOUT_TEXT: 11,
});

export const Kind = Object.freeze({ Standard: 'standard', Option: 'option' });
export const Mapping = Object.freeze({ LinearDepth: 0, LinearLight: 1 });
export const ReliefSide = Object.freeze({ FacingViewer: 0, FacingLamp: 1 });
export const kMaterialCount = 4;
export const kMaterialNames = Object.freeze(['Porcelain', 'Feldspar Porcelain', 'Clay Porcelain', 'White PLA']);
const kMappingNames = Object.freeze(['Linear Depth', 'Linear Light']);
const kReliefNames = Object.freeze(['Facing Viewer', 'Facing Lamp']);

export const kThinnestLow = 0.5;
export const kThinnestHigh = 3.0; // mm
export const kThickestLow = 0.5;
export const kThickestHigh = 6.0; // mm
export const kPlateWidthLow = 20.0;
export const kPlateWidthHigh = 400.0; // mm
export const kBackLightHigh = 16.0;
export const kFrontLightHigh = 2.0;
export const kAngleHigh = 360.0; // degrees
export const kElevationLow = 5.0;
export const kElevationHigh = 90.0; // degrees

/** Back Light's default, from Resolume's demo clips (tools/clips.py defaults, 2026-11-16). */
export const kDefaultBackLight = 7.2146;

/** NaN reads as 0, never as a NaN thickness. `v` is the host's float. */
function clamp01(v) {
  const x = f32(v);
  return x === x ? clamp(x, 0.0, 1.0) : 0.0;
}
function linear(v, low, high) {
  return low + (high - low) * clamp01(v);
}
function inverseLinear(value, low, high) {
  return f32(clamp((value - low) / (high - low), 0.0, 1.0));
}

/** An option's index from the value the host stored: std::lround, half away from zero. */
export function OptionIndex(value, count) {
  const x = f32(value);
  const v = x === x ? x : 0.0;
  const c = clamp(v, -1.0, count);
  const i = c < 0 ? -Math.floor(-c + 0.5) : Math.floor(c + 0.5);
  return clamp(i, 0, count - 1);
}

export const ThinnestFromParam = (v) => linear(v, kThinnestLow, kThinnestHigh);
export const ParamFromThinnest = (mm) => inverseLinear(mm, kThinnestLow, kThinnestHigh);
export const ThickestFromParam = (v) => linear(v, kThickestLow, kThickestHigh);
export const ParamFromThickest = (mm) => inverseLinear(mm, kThickestLow, kThickestHigh);
export const MaterialFromParam = (v) => OptionIndex(v, kMaterialCount);
export const MappingFromParam = (v) => OptionIndex(v, 2);
export const ReliefSideFromParam = (v) => OptionIndex(v, 2);
export const PlateWidthFromParam = (v) => linear(v, kPlateWidthLow, kPlateWidthHigh);
export const ParamFromPlateWidth = (mm) => inverseLinear(mm, kPlateWidthLow, kPlateWidthHigh);
export const BackLightFromParam = (v) => linear(v, 0.0, kBackLightHigh);
export const ParamFromBackLight = (level) => inverseLinear(level, 0.0, kBackLightHigh);
export const FrontLightFromParam = (v) => linear(v, 0.0, kFrontLightHigh);
export const ParamFromFrontLight = (level) => inverseLinear(level, 0.0, kFrontLightHigh);
/** Degrees, 0 from the right, 90 from the top. */
export const LightAngleFromParam = (v) => linear(v, 0.0, kAngleHigh);
export const ParamFromLightAngle = (degrees) => inverseLinear(degrees, 0.0, kAngleHigh);
/** Degrees above the plate. */
export const LightElevationFromParam = (v) => linear(v, kElevationLow, kElevationHigh);
export const ParamFromLightElevation = (degrees) => inverseLinear(degrees, kElevationLow, kElevationHigh);

const standard = (id, name, group, value) => ({ id, name, group, kind: Kind.Standard, defaultValue: f32(value), options: null });
const option = (id, name, group, value, names) => ({ id, name, group, kind: Kind.Option, defaultValue: f32(value), options: names });

// The defaults (SPEC-lithophane.md, Controls): 0.8 and 3 mm and a 100 mm
// plate (3dp.rocks), Sugiyama's body (a), thickness linear in luma, the
// relief toward the viewer, the lamp behind and the room dark; the front lamp
// from the upper left at 45 degrees (chosen); Back Light from the demo clips.
const kTable = Object.freeze([
  standard(PT.THINNEST, 'Thinnest', 'Plate', ParamFromThinnest(0.8)),
  standard(PT.THICKEST, 'Thickest', 'Plate', ParamFromThickest(3.0)),
  option(PT.MATERIAL, 'Material', 'Plate', 0.0, kMaterialNames),
  option(PT.MAPPING, 'Mapping', 'Plate', 0.0, kMappingNames),
  option(PT.RELIEF_SIDE, 'Relief Side', 'Plate', 0.0, kReliefNames),
  standard(PT.PLATE_WIDTH, 'Plate Width', 'Plate', ParamFromPlateWidth(100.0)),
  standard(PT.BACK_LIGHT, 'Back Light', 'Light', ParamFromBackLight(kDefaultBackLight)),
  standard(PT.FRONT_LIGHT, 'Front Light', 'Light', 0.0),
  standard(PT.LIGHT_ANGLE, 'Light Angle', 'Light', ParamFromLightAngle(135.0)),
  standard(PT.LIGHT_ELEVATION, 'Light Elevation', 'Light', ParamFromLightElevation(45.0)),
  standard(PT.MIX, 'Mix', 'Look', 1.0),
]);
if (kTable.length !== PT.ABOUT_TEXT) throw new Error('every control needs a row in the table');

export function InfoOf(id) {
  return kTable[Math.min(id, PT.ABOUT_TEXT - 1)];
}
export const TABLE = kTable;
