// demo/plate.js, printed: the other half of port_numbers.py. The same lines as
// plate_print.cpp, in the same order, from the page's port.
//
//     node plate_print.mjs CASES
import { readFileSync } from 'node:fs';
import * as P from '../../plate.js';

const f32 = Math.fround;
const out = [];
const hex = (v) => {
  const b = new DataView(new ArrayBuffer(4));
  b.setFloat32(0, v);
  return b.getUint32(0).toString(16).padStart(8, '0');
};
// %.17g, as printf writes it.
const g17 = (v) => {
  if (!Number.isFinite(v)) return String(v);
  if (v === 0) return Object.is(v, -0) ? '-0' : '0';
  const exp = Math.floor(Math.log10(Math.abs(v)));
  let s = v.toPrecision(17);
  // toPrecision picks fixed or exponent like %g, but writes e+N where printf writes e+NN.
  if (s.includes('e')) {
    let [m, e] = s.split('e');
    if (m.includes('.')) m = m.replace(/0+$/, '').replace(/\.$/, '');
    const sign = e[0] === '-' ? '-' : '+';
    const digits = e.replace(/^[+-]/, '').padStart(2, '0');
    s = `${m}e${sign}${digits}`;
  } else if (s.includes('.')) {
    s = s.replace(/0+$/, '').replace(/\.$/, '');
  }
  void exp;
  return s;
};
const d = (k, v) => out.push(`${k} d ${g17(v)}`);
const f = (k, v) => out.push(`${k} f ${hex(v)}`);
const i = (k, v) => out.push(`${k} i ${v}`);
const s = (k, v) => out.push(`${k} s ${v}`);
const toString6 = (v) => v.toFixed(6); // std::to_string( double )

for (let id = 0; id < P.PT.ABOUT_TEXT; id += 1) {
  const info = P.InfoOf(id);
  const key = `control.${id}`;
  s(`${key}.name`, info.name);
  s(`${key}.group`, info.group);
  i(`${key}.kind`, info.kind === P.Kind.Option ? 1 : 0);
  f(`${key}.default`, info.defaultValue);
  const options = info.options ?? [];
  i(`${key}.options`, options.length);
  options.forEach((e, n) => s(`${key}.option.${n}`, e));
}

for (let n = -2; n <= 1002; n += 1) {
  const v = f32(n / 1000);
  const key = `conv.${n}`;
  d(`${key}.thinnest`, P.ThinnestFromParam(v));
  d(`${key}.thickest`, P.ThickestFromParam(v));
  d(`${key}.width`, P.PlateWidthFromParam(v));
  d(`${key}.back`, P.BackLightFromParam(v));
  d(`${key}.front`, P.FrontLightFromParam(v));
  d(`${key}.angle`, P.LightAngleFromParam(v));
  d(`${key}.elevation`, P.LightElevationFromParam(v));
  i(`${key}.material`, P.MaterialFromParam(f32(v * 4)));
  i(`${key}.mapping`, P.MappingFromParam(f32(v * 2)));
  i(`${key}.relief`, P.ReliefSideFromParam(f32(v * 2)));
}

for (let m = 0; m < 4; m += 1) {
  const mat = P.MaterialAt(m);
  const key = `material.${m}`;
  s(`${key}.name`, mat.name);
  d(`${key}.Fdr`, mat.Fdr);
  for (let c = 0; c < 3; c += 1) {
    const ck = `${key}.${c}`;
    d(`${ck}.k`, mat.k[c]);
    d(`${ck}.K`, mat.K[c]);
    d(`${ck}.S`, mat.S[c]);
    d(`${ck}.musp`, mat.musp[c]);
    const p = P.DiffusionOf(mat, c);
    d(`${ck}.mua`, p.mua);
    d(`${ck}.zb`, p.zb);
    d(`${ck}.ell`, p.ell);
    d(`${ck}.alpha`, p.alpha);
    d(`${ck}.sigtr`, p.sigtr);
    for (const th of [0.5, 0.8, 1.125, 3.0, 6.0]) {
      d(`${ck}.variance.${toString6(th)}`, P.AxisVariance(p, th));
      for (const r of [0.01, 0.5, 2.0, 10.0]) d(`${ck}.profile.${toString6(th)}.${toString6(r)}`, P.Profile(p, th, r));
    }
  }
  const t = P.TableFor(m);
  d(`${key}.worstL1`, t.worstL1);
  d(`${key}.worstL1At.d`, t.worstL1At[0]);
  d(`${key}.worstL1At.c`, t.worstL1At[1]);
  for (let n = 0; n < t.weights.length; n += 1) d(`${key}.w.${n}`, t.weights[n]);
}

for (let n = 1; n <= 400; n += 1) {
  const k = P.ExactKernel(n * 0.04);
  const key = `kernel.${n}`;
  i(`${key}.size`, k.length);
  k.forEach((v, t) => f(`${key}.${t}`, f32(v)));
}

const cases = readFileSync(process.argv[2], 'utf8').split('\n').filter((l) => l.trim());
cases.forEach((line, index) => {
  const [sm, sthin, sthick, swidth, sw, sh] = line.trim().split(/\s+/);
  const [vm, vthin, vthick, vwidth] = [sm, sthin, sthick, swidth].map((x) => f32(Number(x)));
  const W = Number(sw);
  const H = Number(sh);
  const key = `plan.${index}`;
  const plan = P.PlanFor(P.MaterialFromParam(vm), P.ThinnestFromParam(vthin), P.ThickestFromParam(vthick), P.PlateWidthFromParam(vwidth), W, H, 0);
  d(`${key}.pitch`, plan.pitch);
  d(`${key}.dThin`, plan.dThin);
  d(`${key}.dThick`, plan.dThick);
  f(`${key}.uThin`, f32(plan.dThin));
  f(`${key}.uSpan`, f32(plan.dThick - plan.dThin));
  f(`${key}.uPitch`, f32(plan.pitch));
  i(`${key}.material`, plan.material);
  for (let j = 0; j < P.kScales; j += 1) {
    const sk = `${key}.scale.${j}`;
    i(`${sk}.active`, plan.active[j] ? 1 : 0);
    i(`${sk}.delta`, plan.delta[j] ? 1 : 0);
    i(`${sk}.factor`, plan.factor[j]);
    d(`${sk}.sigmaPx`, plan.sigmaPx[j]);
  }
  i(`${key}.groups`, plan.groups.length);
  plan.groups.forEach((g, n) => {
    const gk = `${key}.group.${n}`;
    i(`${gk}.factor`, g.factor);
    i(`${gk}.count`, g.count);
    for (let q = 0; q < g.count; q += 1) {
      i(`${gk}.scale.${q}`, g.scale[q]);
      i(`${gk}.radius.${q}`, g.radius[q]);
      i(`${gk}.kernelRow.${q}`, g.kernelRow[q]);
    }
    i(`${gk}.lowWidth`, g.lowWidth);
    i(`${gk}.lowHeight`, g.lowHeight);
  });
  i(`${key}.kernelRadius`, plan.kernelRadius);
  i(`${key}.kernelRows`, plan.kernelRows);
  plan.table.forEach((v, n) => f(`${key}.table.${n}`, v));
  plan.kernels.forEach((v, n) => f(`${key}.kernels.${n}`, v));
});

process.stdout.write(`${out.join('\n')}\n`);
