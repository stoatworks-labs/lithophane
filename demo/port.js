/**
 * SW Lithophane's CPU half, part two: the GL orchestration, ported by hand
 * from source/Lithophane.{h,cpp}, source/MultiTarget.{h,cpp} and
 * source/GLState.h's bindUnit / unbindTextureUnits.
 *
 * **Only a reader checks this file against the C++**, and the crosscheck in
 * demo/tools/crosscheck/ (the page's pictures against lptest --pipe, the
 * plugin's own harness driving the real plugin class). It is:
 *
 *   - Shaders.cpp's assembly: kVersion + kQuadVertex for every vertex stage,
 *     kVersion + kCommon + the pass for each fragment stage, in InitGL's
 *     order. demo/tools/check_shaders.py pins both sides.
 *   - MultiTarget: one to four RGBA32F textures, nearest, clamp to edge, drawn
 *     into at once, reallocated only on a change of size or count, cleared
 *     when allocated.
 *   - Lithophane.cpp: the constructor's defaults, InitGL (the four programs,
 *     an empty vertex array, the table texture), ensureBuffers, setKernels,
 *     render (every uniform, every texture unit, every pass, in the plugin's
 *     order) and ProcessOpenGL (the plan's key, the plan remade when it
 *     changes and truncated at kMaxGroups, the table uploaded once a plan).
 *
 * The harness's negative controls (PluginHooks) are not ported: every model
 * uniform is set to the value the hooks default to, which is what ships.
 * The About block is not here; the log is not here.
 */
import { PassBuffer, Program, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';
import {
  f32, PT, InfoOf, PlanFor, TableFor, MaterialAt, kRowOrigin, kRowStep, kRows, kDeltaColumn, kTableWidth,
  kMaxGroups, kMaxGroupScales, MaterialFromParam, ThinnestFromParam, ThickestFromParam, PlateWidthFromParam,
  MappingFromParam, ReliefSideFromParam, BackLightFromParam, FrontLightFromParam, LightAngleFromParam,
  LightElevationFromParam,
} from './plate.js';

const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
const kPi = 3.14159265358979323846;

// ==== PORT: Shaders.h's shared constants ======================================

export const kKernelCentre = 20;
export const kKernelTaps = 2 * kKernelCentre + 1;
export const kMaxLevels = 8;

/** Texture units: the clip, the carve, the table, then the groups. */
const kUnitInput = 0;
const kUnitCarve = 1;
const kUnitTable = 2;
const kUnitLevel = 3;
const kUnitsUsed = kUnitLevel + kMaxLevels;

/** MultiTarget::kMaxTargets. */
export const kMaxTargets = 4;
if (kMaxGroups !== kMaxLevels) throw new Error('the composite reads one texture per group');
if (kMaxGroupScales !== kMaxTargets) throw new Error("a group's scales are one render target each");

// ==== PORT: Shaders.cpp's assembly and Lithophane.cpp's InitGL ================

export const PASSES = ['carve', 'spread', 'pass', 'composite'];

function AssembleVertex() {
  return S.VERSION + S.QUAD_VERTEX;
}

function Assemble(pass) {
  const fragments = {
    carve: S.VERSION + S.COMMON + S.CARVE_FRAGMENT,
    spread: S.VERSION + S.COMMON + S.SPREAD_FRAGMENT,
    pass: S.VERSION + S.COMMON + S.PASS_FRAGMENT,
    composite: S.VERSION + S.COMMON + S.COMPOSITE_FRAGMENT,
  };
  return fragments[pass];
}

/**
 * The four programs, compiled once and shared by every instance on the page.
 * The triangle is made from gl_VertexID: no attributes.
 */
export function compilePrograms(gl) {
  const programs = {};
  for (const pass of PASSES) {
    programs[pass] = new Program(gl, AssembleVertex(pass), Assemble(pass), pass, { attribs: {} });
  }
  return programs;
}

// ==== Uniform setters =========================================================
// glUniform* by name, through the kit's cached glGetUniformLocation; a name the
// program lacks is counted in program.missing, as the kit's own setters count.
// Every integer uniform goes through an integer call: the kit's set() always
// uploads a float, and GL refuses that for an int, ivec or sampler without a
// word.

function at(program, name) {
  const location = program.location(name);
  if (location === null) program.missing.add(name);
  return location;
}
const u1i = (p, name, v) => { const l = at(p, name); if (l !== null) p.gl.uniform1i(l, v | 0); };
const u1f = (p, name, v) => { const l = at(p, name); if (l !== null) p.gl.uniform1f(l, v); };
const u2i = (p, name, a, b) => { const l = at(p, name); if (l !== null) p.gl.uniform2i(l, a | 0, b | 0); };
const u3f = (p, name, a, b, c) => { const l = at(p, name); if (l !== null) p.gl.uniform3f(l, a, b, c); };
const u4i = (p, name, a, b, c, d) => { const l = at(p, name); if (l !== null) p.gl.uniform4i(l, a | 0, b | 0, c | 0, d | 0); };
const u1fv = (p, name, v) => { const l = at(p, name); if (l !== null) p.gl.uniform1fv(l, v); };
const u1iv = (p, name, v) => { const l = at(p, name); if (l !== null) p.gl.uniform1iv(l, v); };

// ==== PORT: GLState.h =========================================================

function bindUnit(gl, unit, texture) {
  bindTexture(gl, unit, texture);
}
function unbindTextureUnits(gl, count) {
  for (let unit = count - 1; unit >= 0; unit -= 1) {
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, null);
  }
}

// ==== PORT: MultiTarget.h / MultiTarget.cpp ===================================

export class MultiTarget {
  constructor(gl) {
    this.gl = gl;
    this.fbo = null;
    this.textures = [null, null, null, null];
    this.width = 0;
    this.height = 0;
    this.count = 0;
    this.format = 0;
  }

  Ensure(requestedWidth, requestedHeight, requestedCount, requestedFormat = this.gl.RGBA32F) {
    const gl = this.gl;
    if (requestedWidth <= 0 || requestedHeight <= 0 || requestedCount < 1 || requestedCount > kMaxTargets) return false;
    if (this.fbo !== null && this.width === requestedWidth && this.height === requestedHeight
        && this.count === requestedCount && this.format === requestedFormat) return true;

    this.Destroy();
    const previousFBO = gl.getParameter(gl.FRAMEBUFFER_BINDING);
    this.fbo = gl.createFramebuffer();
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
    const buffers = [];
    for (let i = 0; i < requestedCount; i += 1) {
      const t = gl.createTexture();
      this.textures[i] = t;
      gl.bindTexture(gl.TEXTURE_2D, t);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      gl.texStorage2D(gl.TEXTURE_2D, 1, requestedFormat, requestedWidth, requestedHeight);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0 + i, gl.TEXTURE_2D, t, 0);
      buffers.push(gl.COLOR_ATTACHMENT0 + i);
    }
    gl.bindTexture(gl.TEXTURE_2D, null);
    gl.drawBuffers(buffers);
    const complete = gl.checkFramebufferStatus(gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE;
    gl.bindFramebuffer(gl.FRAMEBUFFER, previousFBO);
    if (!complete) {
      this.Destroy();
      return false;
    }
    this.width = requestedWidth;
    this.height = requestedHeight;
    this.count = requestedCount;
    this.format = requestedFormat;
    this.Clear();
    return true;
  }

  Clear() {
    const gl = this.gl;
    if (this.fbo === null) return;
    const previousFBO = gl.getParameter(gl.FRAMEBUFFER_BINDING);
    const previousViewport = gl.getParameter(gl.VIEWPORT);
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.fbo);
    gl.viewport(0, 0, this.width, this.height);
    gl.clearColor(0.0, 0.0, 0.0, 0.0);
    gl.clear(gl.COLOR_BUFFER_BIT);
    gl.bindFramebuffer(gl.FRAMEBUFFER, previousFBO);
    gl.viewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
  }

  Destroy() {
    const gl = this.gl;
    for (let i = 0; i < kMaxTargets; i += 1) {
      if (this.textures[i] !== null) gl.deleteTexture(this.textures[i]);
      this.textures[i] = null;
    }
    if (this.fbo !== null) gl.deleteFramebuffer(this.fbo);
    this.fbo = null;
    this.width = 0;
    this.height = 0;
    this.count = 0;
    this.format = 0;
  }

  FramebufferID() { return this.fbo; }
  TextureID(i) { return i >= 0 && i < kMaxTargets ? this.textures[i] : null; }
  Width() { return this.width; }
  Height() { return this.height; }
  Count() { return this.count; }
}

// ==== PORT: Lithophane.h / Lithophane.cpp =====================================

function makeDataTexture(gl) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return texture;
}

function uploadData(gl, texture, width, height, texels) {
  gl.bindBuffer(gl.PIXEL_UNPACK_BUFFER, null);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
  gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0);
  gl.pixelStorei(gl.UNPACK_SKIP_ROWS, 0);
  // WebGL's own unpack state, which GL does not have: both off.
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, width, height, 0, gl.RGBA, gl.FLOAT, texels);
  gl.bindTexture(gl.TEXTURE_2D, null);
}

const kLevelNames = ['Level0', 'Level1', 'Level2', 'Level3', 'Level4', 'Level5', 'Level6', 'Level7'];

/**
 * One SW Lithophane instance. `params` is the plugin's float params[], a
 * Float32Array so that a value rounds as the host's float does. The hooks are
 * PluginHooks' defaults, fixed: the page has no negative controls.
 */
export class Lithophane {
  constructor(gl, programs) {
    this.gl = gl;
    this.programs = programs;
    this.params = new Float32Array(PT.ABOUT_TEXT);
    for (let id = 0; id < PT.ABOUT_TEXT; id += 1) this.params[id] = InfoOf(id).defaultValue;

    this.vao = null;
    this.tableTexture = null;
    this.tableUploaded = false;
    this.carve = new PassBuffer(gl, { filter: 'nearest' });
    this.fullSpread = new MultiTarget(gl);
    this.stages = [];
    for (let i = 0; i < kMaxGroups; i += 1) {
      this.stages.push({ spread: new MultiTarget(gl), decimated: new MultiTarget(gl), across: new MultiTarget(gl), result: new MultiTarget(gl) });
    }
    this.plan = null;
    this.planKey = new Float64Array(7);
    this.planValid = false;
    this.frameIndex = 0;
    /** Set by ProcessOpenGL when the plan has more groups than the composite reads (the plugin logs it). */
    this.truncated = false;
    /** Set by render when a buffer could not be allocated (the plugin logs it). */
    this.buffersFailed = false;
  }

  InitGL() {
    const gl = this.gl;
    // A core profile draws nothing without a vertex array bound; the triangle
    // needs no attributes.
    this.vao = gl.createVertexArray();
    this.tableTexture = makeDataTexture(gl);
    this.tableUploaded = false;
    return true;
  }

  DeInitGL() {
    const gl = this.gl;
    if (this.vao) gl.deleteVertexArray(this.vao);
    this.vao = null;
    if (this.tableTexture) gl.deleteTexture(this.tableTexture);
    this.tableTexture = null;
    this.tableUploaded = false;
    this.carve.dispose();
    this.fullSpread.Destroy();
    for (const s of this.stages) for (const t of [s.spread, s.decimated, s.across, s.result]) t.Destroy();
  }

  /** The About buttons are not here: a web page has links of its own. */
  SetFloatParameter(index, value) {
    if (index < 0 || index >= PT.ABOUT_TEXT) return false;
    this.params[index] = value;
    return true;
  }

  GetFloatParameter(index) {
    return index >= 0 && index < PT.ABOUT_TEXT ? this.params[index] : 0.0;
  }

  /** The plugin's state of time is not used (SetTimeSupported( false ); the timeLeak hook is off). */
  SetTime() {
    return true;
  }

  ensureBuffers(width, height) {
    const gl = this.gl;
    let ok = true;
    try {
      this.carve.ensure(width, height, gl.RGBA32F);
    } catch {
      ok = false;
    }

    // One shared target for the full-resolution groups, as many textures as
    // the largest of them needs; none if there are none.
    let fullCount = 0;
    for (const g of this.plan.groups) if (g.factor === 1) fullCount = Math.max(fullCount, g.count);
    if (fullCount > 0) ok = ok && this.fullSpread.Ensure(width, height, fullCount);
    else this.fullSpread.Destroy();

    for (let i = 0; i < kMaxGroups; i += 1) {
      const s = this.stages[i];
      if (i >= this.plan.groups.length) {
        for (const t of [s.spread, s.decimated, s.across, s.result]) t.Destroy();
        continue;
      }
      const g = this.plan.groups[i];
      if (g.factor === 1) {
        for (const t of [s.spread, s.decimated, s.across]) t.Destroy();
        ok = ok && s.result.Ensure(width, height, 1);
        continue;
      }
      ok = ok && s.spread.Ensure(g.lowWidth, height, g.count);
      ok = ok && s.decimated.Ensure(g.lowWidth, g.lowHeight, g.count);
      ok = ok && s.across.Ensure(g.lowWidth, g.lowHeight, g.count);
      ok = ok && s.result.Ensure(g.lowWidth, g.lowHeight, 1);
    }
    return ok;
  }

  bindTarget(fbo, width, height) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    gl.viewport(0, 0, width, height);
  }

  setKernels(shader, group) {
    const plan = this.plan;
    const k = new Float32Array(kMaxGroupScales * kKernelTaps);
    let widest = 0;
    const stride = 2 * plan.kernelRadius + 1;
    for (let s = 0; s < group.count; s += 1) {
      const R = Math.min(group.radius[s], kKernelCentre);
      widest = Math.max(widest, R);
      for (let o = -R; o <= R; o += 1) {
        k[s * kKernelTaps + o + kKernelCentre] = plan.kernels[(group.kernelRow[s] * stride + (o + plan.kernelRadius)) * 4];
      }
    }
    u1fv(shader, 'Kernel', k);
    u1i(shader, 'Radius', widest);
  }

  render(input, hostViewport, hostFBO, width, height) {
    const gl = this.gl;
    const P = this.programs;
    const plan = this.plan;
    // The plugin logs a failed allocation and draws nothing; the frame still
    // returns FF_SUCCESS.
    this.buffersFailed = !this.ensureBuffers(width, height);
    if (this.buffersFailed) return;
    gl.bindVertexArray(this.vao);

    const material = MaterialFromParam(this.params[PT.MATERIAL]);
    const mat = MaterialAt(material);
    const span = plan.dThick - plan.dThin;
    const decimationKind = 1; // hooks.decimationModel 0

    const setTable = (s) => {
      u1i(s, 'Table', kUnitTable);
      u1f(s, 'RowOrigin', f32(kRowOrigin));
      u1f(s, 'RowStep', f32(kRowStep));
      u1i(s, 'RowCount', kRows);
    };
    bindUnit(gl, kUnitTable, this.tableTexture);

    // 1. carve
    {
      const s = P.carve;
      s.use();
      bindUnit(gl, kUnitInput, input.texture);
      u1i(s, 'InputTexture', kUnitInput);
      setTable(s);
      u1i(s, 'Mapping', MappingFromParam(this.params[PT.MAPPING]));
      u1f(s, 'Thin', f32(plan.dThin));
      u1f(s, 'Span', f32(span));
      u3f(s, 'Coefficient', f32(mat.k[0]), f32(mat.k[1]), f32(mat.k[2]));
      u1f(s, 'LightK', f32(mat.k[1]));
      u1f(s, 'MinT', f32(Math.exp(-mat.k[1] * span)));
      u1i(s, 'AttenuationModel', 0);
      u1i(s, 'Premultiply', 1);
      u1i(s, 'ClampOn', 1);
      this.bindTarget(this.carve.fbo, width, height);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    // 2. per group: spread along x from the carve, then the passes
    bindUnit(gl, kUnitCarve, this.carve.texture);
    for (let gi = 0; gi < plan.groups.length; gi += 1) {
      const g = plan.groups[gi];
      const st = this.stages[gi];
      const full = g.factor === 1;

      {
        const s = P.spread;
        s.use();
        u1i(s, 'Carve', kUnitCarve);
        setTable(s);
        u1i(s, 'Count', g.count);
        u4i(s, 'Column', g.scale[0], g.scale[1], g.scale[2], g.scale[3]);
        u1i(s, 'Kind', full ? 0 : decimationKind);
        u1i(s, 'Factor', g.factor);
        u1i(s, 'SourceWidth', width);
        u1i(s, 'EdgeModel', 0);
        this.setKernels(s, g);
        if (full) this.bindTarget(this.fullSpread.FramebufferID(), width, height);
        else this.bindTarget(st.spread.FramebufferID(), g.lowWidth, height);
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }

      const s = P.pass;
      s.use();
      u1i(s, 'Source0', 4);
      u1i(s, 'Source1', 5);
      u1i(s, 'Source2', 6);
      u1i(s, 'Source3', 7);
      u1i(s, 'Count', g.count);
      u1i(s, 'Factor', g.factor);
      u1i(s, 'EdgeModel', 0);
      this.setKernels(s, g);
      const full1 = [{ from: this.fullSpread, to: st.result, kind: 0, dx: 0, dy: 1, sum: 1 }];
      const low = [
        { from: st.spread, to: st.decimated, kind: decimationKind, dx: 0, dy: 1, sum: 0 },
        { from: st.decimated, to: st.across, kind: 0, dx: 1, dy: 0, sum: 0 },
        { from: st.across, to: st.result, kind: 0, dx: 0, dy: 1, sum: 1 },
      ];
      for (const step of full ? full1 : low) {
        // A sampler the draw never reads still needs a complete texture
        // bound, or Apple's driver logs it as unloadable: the first.
        for (let t = 0; t < kMaxTargets; t += 1) {
          bindUnit(gl, 4 + t, t < step.from.Count() ? step.from.TextureID(t) : step.from.TextureID(0));
        }
        u1i(s, 'Kind', step.kind);
        u2i(s, 'Direction', step.dx, step.dy);
        u2i(s, 'SourceSize', step.from.Width(), step.from.Height());
        u1i(s, 'Sum', step.sum);
        this.bindTarget(step.to.FramebufferID(), step.to.Width(), step.to.Height());
        gl.drawArrays(gl.TRIANGLES, 0, 3);
      }
    }

    // 3. composite, into the host's framebuffer and viewport
    {
      gl.bindFramebuffer(gl.FRAMEBUFFER, hostFBO);
      gl.viewport(hostViewport[0], hostViewport[1], hostViewport[2], hostViewport[3]);
      const s = P.composite;
      s.use();
      bindUnit(gl, kUnitInput, input.texture);
      bindUnit(gl, kUnitCarve, this.carve.texture);
      bindUnit(gl, kUnitTable, this.tableTexture);
      const factors = new Int32Array(kMaxLevels);
      for (let i = 0; i < kMaxLevels; i += 1) {
        const used = i < plan.groups.length;
        bindUnit(gl, kUnitLevel + i, used ? this.stages[i].result.TextureID(0) : this.carve.texture);
        u1i(s, kLevelNames[i], kUnitLevel + i);
        factors[i] = used ? plan.groups[i].factor : 1;
      }
      u1i(s, 'InputTexture', kUnitInput);
      u1i(s, 'Carve', kUnitCarve);
      setTable(s);
      u1i(s, 'LevelCount', plan.groups.length);
      u1iv(s, 'LevelFactor', factors);
      u1i(s, 'DeltaColumn', kDeltaColumn);
      u1i(s, 'Reconstruction', 0);
      u2i(s, 'ViewOrigin', hostViewport[0], hostViewport[1]);
      u2i(s, 'Size', width, height);
      u1f(s, 'BackLight', f32(BackLightFromParam(this.params[PT.BACK_LIGHT])));
      u1f(s, 'FrontLight', f32(FrontLightFromParam(this.params[PT.FRONT_LIGHT])));
      const azimuth = LightAngleFromParam(this.params[PT.LIGHT_ANGLE]); // the timeLeak hook is off
      const theta = (azimuth * kPi) / 180.0;
      const e = (LightElevationFromParam(this.params[PT.LIGHT_ELEVATION]) * kPi) / 180.0;
      u3f(s, 'LightDir', f32(Math.cos(e) * Math.cos(theta)), f32(Math.cos(e) * Math.sin(theta)), f32(Math.sin(e)));
      u1f(s, 'Pitch', f32(plan.pitch));
      u1i(s, 'Relief', ReliefSideFromParam(this.params[PT.RELIEF_SIDE])); // the reliefIgnored hook is off
      u3f(s, 'KmK', f32(mat.K[0]), f32(mat.K[1]), f32(mat.K[2]));
      u3f(s, 'KmS', f32(mat.S[0]), f32(mat.S[1]), f32(mat.S[2]));
      u1i(s, 'FrontModel', 0);
      u1f(s, 'MixAmount', clamp(this.params[PT.MIX], 0.0, 1.0));
      u1i(s, 'AlphaModel', 0);
      u1i(s, 'ClampOn', 1);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }

    unbindTextureUnits(gl, kUnitsUsed);
    gl.activeTexture(gl.TEXTURE0);
    gl.useProgram(null);
    gl.bindVertexArray(null);
  }

  /** The plan's inputs, as ProcessOpenGL keys them. */
  PlanInputs(width, height) {
    return {
      material: MaterialFromParam(this.params[PT.MATERIAL]),
      thin: ThinnestFromParam(this.params[PT.THINNEST]),
      thick: ThickestFromParam(this.params[PT.THICKEST]),
      plateWidth: PlateWidthFromParam(this.params[PT.PLATE_WIDTH]),
      width,
      height,
    };
  }

  /**
   * ProcessOpenGL. `input` is { texture, width, height }, the host's clip;
   * `hostFBO` is the host's framebuffer (null: the canvas) and `hostViewport`
   * its viewport. Returns false where the plugin returns FF_FAIL.
   *
   * The plugin fits a material's table the first time a plan needs it
   * (TableFor). So does this, on the calling thread; the page fits in a Worker
   * first and only calls this once the table is there (plugin.js).
   */
  ProcessOpenGL(input, hostFBO = null, hostViewport = null) {
    const gl = this.gl;
    if (!input || !input.texture) return false;
    gl.disable(gl.BLEND);

    const width = input.width | 0;
    const height = input.height | 0;
    if (width <= 0 || height <= 0 || this.vao === null) return false;
    const viewport = hostViewport ?? [0, 0, width, height];

    // The plan is a pure function of these; it is remade when one changes.
    const { material, thin, thick, plateWidth } = this.PlanInputs(width, height);
    const key = [material, thin, thick, plateWidth, width, height, 0];
    if (!this.planValid || key.some((v, i) => !Object.is(v, this.planKey[i]))) {
      TableFor(material);
      this.plan = PlanFor(material, thin, thick, plateWidth, width, height, 0);
      this.truncated = this.plan.groups.length > kMaxGroups;
      if (this.truncated) this.plan.groups.length = kMaxGroups;
      this.planKey.set(key);
      this.planValid = true;
      this.tableUploaded = false;
    }
    if (!this.tableUploaded) {
      uploadData(gl, this.tableTexture, kTableWidth, kRows, this.plan.table);
      this.tableUploaded = true;
    }

    this.render(input, viewport, hostFBO, width, height);
    this.frameIndex += 1;
    return true;
  }
}
