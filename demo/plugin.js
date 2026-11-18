/**
 * SW Lithophane (LP01, an FFGL effect), in a browser.
 *
 * The clip carved into a backlit porcelain plate: thick where the picture is
 * dark, thin where it is light, each channel absorbed as exp(-k d) on the way
 * through and spread by the multipole diffusion profile of the thickness it
 * crossed (fifteen Gaussians, fitted per material); a front lamp lights the
 * carved face by Lambert's law over the Kubelka-Munk reflectance of the plate.
 *
 * Four files, and they are not equally faithful:
 *
 *   **shaders.js is the plugin's own GLSL**, unedited: spliced from
 *   source/Shaders.cpp by demo/tools/check_shaders.py --write, and the same
 *   script fails tools/verify.sh if a character, or the way the pieces are
 *   joined, drifts.
 *
 *   **plate.js and port.js are a hand port of the CPU half, and only a reader
 *   checks them** (with the crosscheck in demo/tools/crosscheck/): plate.js is
 *   Plate.cpp and Controls.cpp (the materials, the profile, the fit, the
 *   kernels, the plan, the controls), port.js is Lithophane.cpp's frame and
 *   MultiTarget. fit-worker.js runs plate.js's fit off the main thread.
 *
 *   **This file is the page**: the parameter panel, the harness's clip ported
 *   to GLSL, the fitting status and the line under the picture, the page's
 *   presets and the hook the crosscheck drives. None of it is the plugin.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **The fit runs in a Worker, and nothing is drawn until it is done.** The
 * plugin fits a material's weight table the first time a frame needs it, on
 * the host's render thread (TableFor, a tenth of a second or so). Here the
 * same code runs in a module Worker, the material on screen first and the
 * rest after it, and the picture is left empty, with "Fitting ..." under it,
 * until that material's table is back. There is no precomputed table: a
 * table this page did not fit would be a claim about the plugin nobody
 * checked. If the Worker cannot start, the fit runs on the main thread and
 * the page says so.
 *
 * **The default clip is the harness's own** (lptest's buildCard: schlieren's
 * look card), ported to GLSL below, so the page opens on the picture
 * lptest's checks and the README's numbers are made over.
 *
 * **The About block is not a parameter here**, as on every page in the suite.
 */
import { mountDemo } from './vendor/demo.js';
import { SOURCES, SourceRenderer } from './vendor/sources.js';
import { PassBuffer } from './vendor/gl.js';
import {
  f32, PT, TABLE, Kind, kMaterialNames, Mapping, ReliefSide, HasTable, SetTable, TableFor, MaterialAt,
  MaterialFromParam, ThinnestFromParam, ParamFromThinnest, ThickestFromParam, ParamFromThickest,
  PlateWidthFromParam, ParamFromPlateWidth, BackLightFromParam, ParamFromBackLight, FrontLightFromParam,
  ParamFromFrontLight, LightAngleFromParam, ParamFromLightAngle, LightElevationFromParam,
  ParamFromLightElevation, kMaxGroups,
} from './plate.js';
import { Lithophane, compilePrograms } from './port.js';

//===========================================================================
// The harness's clip: tools/lptest/main.cpp's buildCard, in GLSL. The look
// card (schlieren's): a sunset sky, a sun, a skyline with lit windows, a
// stripe of 75% bars and a white title block. Nothing moves. Rows bottom
// first, as there. lptest builds it in double and uploads floats; this draws
// it in float into the kit's 8-bit clip, and the crosscheck counts the pixels
// where the two differ.
//===========================================================================
const CARD = `
uniform vec2 Resolution;
in vec2 uv;
out vec4 fragColor;

uint pcgCard( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

float hash01( uint a, uint b )
{
	return float( pcgCard( a * 2654435761u ^ pcgCard( b + 0x9e3779b9u ) ) ) * ( 1.0 / 4294967296.0 );
}

void main()
{
	vec2 px  = floor( gl_FragCoord.xy );
	float w  = Resolution.x;
	float h  = Resolution.y;
	float s  = min( w, h );
	float u  = ( px.x + 0.5 - 0.5 * w ) / s;
	float v  = ( px.y + 0.5 - 0.5 * h ) / s;
	float k  = clamp( 0.5 + v, 0.0, 1.0 );
	vec3 c   = vec3( 0.95 - 0.8 * k, 0.45 - 0.3 * k, 0.25 + 0.5 * k );
	float d  = sqrt( ( u + 0.25 ) * ( u + 0.25 ) + ( v + 0.02 ) * ( v + 0.02 ) );
	if( d < 0.2 )
		c = vec3( 1.0, 0.85 - 0.5 * d, 0.35 );
	int block = int( floor( ( u + 2.0 ) * 11.0 ) );
	float top = -0.12 + 0.16 * hash01( uint( block ), 7u );
	if( v < top )
	{
		c = vec3( 0.05 );
		int wx = int( floor( ( u + 2.0 ) * 70.0 ) );
		int wy = int( floor( v * 60.0 + 100.0 ) );
		if( ( wx % 3 ) != 0 && ( wy % 2 ) == 0 && hash01( uint( wx * 131 + wy ), 9u ) > 0.55 )
			c = vec3( 1.0, 0.9, 0.55 );
	}
	if( v < -0.36 )
	{
		int bar = int( clamp( floor( ( px.x + 0.5 ) / w * 7.0 ), 0.0, 6.0 ) );
		vec3 b  = vec3( 0.0, 0.0, 1.0 );
		if( bar == 0 ) b = vec3( 1.0, 1.0, 1.0 );
		else if( bar == 1 ) b = vec3( 1.0, 1.0, 0.0 );
		else if( bar == 2 ) b = vec3( 0.0, 1.0, 1.0 );
		else if( bar == 3 ) b = vec3( 0.0, 1.0, 0.0 );
		else if( bar == 4 ) b = vec3( 1.0, 0.0, 1.0 );
		else if( bar == 5 ) b = vec3( 1.0, 0.0, 0.0 );
		c = 0.75 * b;
	}
	if( abs( u - 0.42 ) < 0.28 && abs( v - 0.3 ) < 0.06 )
		c = vec3( 0.95 );
	fragColor = vec4( c, 1.0 );
}
`;

if (!SOURCES.some((s) => s.id === 'card')) {
  SOURCES.unshift({
    id: 'card',
    name: 'The harness’s card',
    hint: 'The plugin harness’s own clip (buildCard in lptest), drawn in the page: a sunset sky, a sun, a skyline with lit windows, a stripe of 75% bars and a white block. Nothing moves.',
    fragment: CARD,
  });
}

//===========================================================================
// The parameters: the plugin's own, in kTable's order and groups, with its
// names, element lists and defaults. Hints from the README and AGENTS.md.
//===========================================================================

const V = (v) => f32(v);
const mm = (x) => `${x.toFixed(2)} mm`;
const deg = (x) => `${x.toFixed(1)}°`;

const SPEC = [
  ['thinnest', PT.THINNEST, (v) => mm(ThinnestFromParam(V(v))), 'The plate’s thickness under white, 0.5 to 3 mm. A thinner plate passes more light and blurs less. If Thickest is set below it, the plate is Thinnest everywhere.'],
  ['thickest', PT.THICKEST, (v) => mm(ThickestFromParam(V(v))), 'The plate’s thickness under black (and under a transparent pixel), 0.5 to 6 mm. Thick porcelain passes little blue, so the dark parts come through red and blurred.'],
  ['material', PT.MATERIAL, null, 'Porcelain, Feldspar Porcelain and Clay Porcelain are the three Japanese porcelain bodies Sugiyama et al. measured (their absorption and scattering per channel); White PLA is a printed lithophane, its coefficients chosen, not measured (AGENTS.md says how). Each has its own 15-Gaussian fit, made the first time it is chosen.'],
  ['mapping', PT.MAPPING, null, 'How the picture becomes thickness. Linear Depth: thickness linear in the clip’s luma, as 3dp.rocks’ generator carves. Linear Light: Cura’s, so the green transmission is linear in the clip’s 2.2-gamma luminance.'],
  ['reliefSide', PT.RELIEF_SIDE, null, 'Which face is carved. Under back light it changes nothing (the model spreads each column’s light by its own thickness, which cannot tell the faces apart); it changes only the front-lit relief.'],
  ['plateWidth', PT.PLATE_WIDTH, (v) => `${PlateWidthFromParam(V(v)).toFixed(0)} mm`, 'The plate’s width across the frame, 20 to 400 mm. The scattering is in millimetres, so a small plate blurs the picture over more pixels than a large one.'],
  ['backLight', PT.BACK_LIGHT, (v) => BackLightFromParam(V(v)).toFixed(3), 'The lamp behind the plate, 0 to 16. The default, 7.2146, was set on Resolume’s own demo clips so that the brightest 1% of the median clip just reaches full scale in red.'],
  ['frontLight', PT.FRONT_LIGHT, (v) => FrontLightFromParam(V(v)).toFixed(3), 'A lamp in front, 0 to 2, on the carved face: Lambert’s law over the plate’s Kubelka–Munk reflectance. No shadows and no gloss; the porcelain is unglazed biscuit.'],
  ['lightAngle', PT.LIGHT_ANGLE, (v) => deg(LightAngleFromParam(V(v))), 'Where the front lamp is around the plate, 0 to 360°: 0 from the right, 90 from the top. The default, 135°, is the upper left (chosen).'],
  ['lightElevation', PT.LIGHT_ELEVATION, (v) => deg(LightElevationFromParam(V(v))), 'The front lamp’s height above the plate, 5 to 90°. Low light rakes the relief; 90° lights it flat.'],
  ['mix', PT.MIX, (v) => `×${V(v).toFixed(2)}`, 'The plate over the clip, blended premultiplied. At 1 the output is opaque; 0 hands the clip through untouched.'],
];

const PARAMS = SPEC.map(([id, slot, display, hint]) => {
  const row = TABLE[slot];
  const types = { [Kind.Standard]: 'standard', [Kind.Option]: 'option' };
  const p = { id, slot, name: row.name, type: types[row.kind], default: row.defaultValue, group: row.group, hint };
  if (row.options) p.elements = [...row.options];
  if (display) p.display = display;
  return p;
});
if (PARAMS.length !== PT.ABOUT_TEXT || PARAMS.some((p, i) => p.slot !== i)) {
  throw new Error('the page’s parameters are not the plugin’s table, in order');
}

//===========================================================================
// The fit: a module Worker running plate.js's own TableFor, the material on
// screen first. The plugin does the same fit on the host's render thread.
//===========================================================================

const fits = {
  state: kMaterialNames.map(() => 'waiting'), // waiting | fitting | done | failed
  ms: kMaterialNames.map(() => null),
  where: null, // 'worker' | 'main thread'
  queue: [],
  busy: false,
  worker: null,
  onDone: null,
};

function startWorker() {
  try {
    const worker = new Worker(new URL('./fit-worker.js', import.meta.url), { type: 'module' });
    worker.onmessage = (event) => {
      const { material, table, ms, error } = event.data;
      fits.busy = false;
      if (error) {
        fits.state[material] = 'failed';
        console.error(`the fit of ${kMaterialNames[material]} failed in the Worker: ${error}`);
      } else {
        SetTable(material, table);
        fits.state[material] = 'done';
        fits.ms[material] = ms;
      }
      fits.onDone?.(material);
      pump();
    };
    worker.onerror = (event) => {
      // A Worker that cannot load (no module Workers, a blocked script): the
      // fit moves to the main thread, and the page says so.
      event.preventDefault?.();
      console.warn('the fit Worker failed; fitting on the main thread instead', event.message ?? '');
      worker.terminate();
      fits.worker = null;
      fits.where = 'main thread';
      fits.busy = false;
      // Whatever it was fitting goes back to the front of the queue.
      const lost = fits.state.flatMap((s, m) => (s === 'fitting' ? [m] : []));
      for (const m of lost) fits.state[m] = 'waiting';
      fits.queue = [...lost, ...fits.queue.filter((m) => !lost.includes(m))];
      pump();
    };
    fits.worker = worker;
    fits.where = 'worker';
  } catch (error) {
    console.warn('no module Worker; fitting on the main thread instead', error);
    fits.worker = null;
    fits.where = 'main thread';
  }
}

function pump() {
  if (fits.busy) return;
  const next = fits.queue.find((m) => fits.state[m] === 'waiting');
  if (next === undefined) return;
  fits.queue = fits.queue.filter((m) => m !== next);
  fits.state[next] = 'fitting';
  fits.busy = true;
  if (fits.worker) {
    fits.worker.postMessage({ material: next });
    return;
  }
  // The main thread: after this frame is painted, so "Fitting" shows first.
  setTimeout(() => {
    const t0 = performance.now();
    try {
      TableFor(next);
      fits.state[next] = 'done';
      fits.ms[next] = performance.now() - t0;
    } catch (error) {
      fits.state[next] = 'failed';
      console.error(error);
    }
    fits.busy = false;
    fits.onDone?.(next);
    pump();
  }, 30);
}

/** Ask for a material's table, ahead of the rest. */
function wantFit(material) {
  if (HasTable(material)) return;
  if (fits.state[material] === 'waiting') {
    fits.queue = [material, ...fits.queue.filter((m) => m !== material)];
  }
  pump();
}

//===========================================================================
// For demo/tools/crosscheck/ and headless checks.
//===========================================================================

/** std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ): lptest's own rounding of its float output, the product a float. */
const toCode = (v) => {
  const x = Math.fround(Math.min(1, Math.max(0, v)) * 255);
  return Math.floor(x + 0.5);
};

function installHook(gl, programs, clips) {
  const byName = new Map(TABLE.map((row) => [row.name, row]));
  const setByName = (instance, name, value) => {
    const row = byName.get(name);
    if (!row) throw new Error(`no parameter called '${name}'`);
    const index = row.options ? row.options.indexOf(String(value)) : -1;
    const number = index >= 0 ? index : Number(value);
    if (!Number.isFinite(number)) throw new Error(`'${value}' is not a number or an option of ${name}`);
    instance.SetFloatParameter(row.id, number);
  };

  /** A float RGBA target, read back and rounded as lptest rounds its own, top row first. */
  const readCodes = (fbo, width, height) => {
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    const floats = new Float32Array(width * height * 4);
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.FLOAT, floats);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const row = width * 4;
    const out = new Uint8Array(row * height);
    for (let y = 0; y < height; y += 1) {
      const src = (height - 1 - y) * row;
      const dst = y * row;
      for (let x = 0; x < row; x += 1) out[dst + x] = toCode(floats[src + x]);
    }
    return out;
  };

  /** An RGBA8 target (what the page's canvas is), read back as it stands, top row first. */
  const readBytes = (fbo, width, height) => {
    gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
    const read = new Uint8Array(width * height * 4);
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, read);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const row = width * 4;
    const out = new Uint8Array(read.length);
    for (let y = 0; y < height; y += 1) out.set(read.subarray(y * row, (y + 1) * row), (height - 1 - y) * row);
    return out;
  };

  /** The uniforms a program declares that nothing has set: each would sit at 0. */
  const unsetUniforms = (program) => {
    const set = new Set([...program.locations].filter(([, loc]) => loc !== null).map(([name]) => name));
    const out = [];
    const count = gl.getProgramParameter(program.program, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < count; i += 1) {
      const name = gl.getActiveUniform(program.program, i).name;
      // An array is active as `name[0]`; port.js sets it by `name`.
      if (!set.has(name) && !set.has(name.replace(/\[0\]$/, ''))) out.push(name);
    }
    return out;
  };

  window.lithophaneDemo = {
    table: TABLE.map((row) => ({ name: row.name, kind: row.kind, value: row.defaultValue, elements: row.options ?? [] })),

    /**
     * A FRESH instance (lptest's is one too), with the settings by the
     * plugin's display name (an option by its element's name).
     *
     * Each frame() hands the plugin one picture -- an RGBA8 one, top row first,
     * as lptest --pipe reads it, or with `clip` set the page's own clip of that
     * id drawn at `time` -- runs ProcessOpenGL into the host framebuffer and
     * reads it back, top row first. `host` picks that framebuffer: 'float',
     * RGBA32F and rounded as lptest rounds its own (lptest's is RGBA32F), or
     * 'rgba8', what the page's canvas is. `changes` are set before the frame,
     * as lptest's --script cues are. A material's table is fitted here, on
     * this thread, if the page has not fitted it yet.
     */
    begin({ settings = {}, width, height, host = 'float', clip = null, input = 'rgba8' }) {
      const run = new Lithophane(gl, programs);
      run.InitGL();
      for (const [name, value] of Object.entries(settings)) setByName(run, name, value);
      const source = clip ? SOURCES.find((s) => s.id === clip) : null;
      if (clip && !source) throw new Error(`no clip '${clip}'`);
      // The page's clips are RGBA8 textures. lptest's is RGBA32F, holding the
      // same codes / 255: `input: 'float'` uploads it that way, to tell a
      // difference of representation from one of arithmetic.
      const floats = input === 'float';
      const upload = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, upload);
      gl.texStorage2D(gl.TEXTURE_2D, 1, floats ? gl.RGBA32F : gl.RGBA8, width, height);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.bindTexture(gl.TEXTURE_2D, null);
      const target = new PassBuffer(gl, { filter: 'nearest' }).ensure(width, height, host === 'rgba8' ? gl.RGBA8 : gl.RGBA32F);
      const flipped = new Uint8Array(width * height * 4);
      const asFloats = floats ? new Float32Array(width * height * 4) : null;
      const row = width * 4;
      return {
        frame(rgba, time = 0, changes = {}) {
          for (const [name, value] of Object.entries(changes)) setByName(run, name, value);
          let picture;
          if (source) {
            picture = clips.render(source, width, height, time);
          } else {
            for (let y = 0; y < height; y += 1) flipped.set(rgba.subarray(y * row, (y + 1) * row), (height - 1 - y) * row);
            gl.bindTexture(gl.TEXTURE_2D, upload);
            gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
            gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
            gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
            if (floats) {
              // in[ ... ] / 255.0f, as lptest --pipe divides (a float division, correctly rounded).
              for (let i = 0; i < flipped.length; i += 1) asFloats[i] = Math.fround(Math.fround(flipped[i]) / Math.fround(255));
              gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.FLOAT, asFloats);
            } else {
              gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, flipped);
            }
            gl.bindTexture(gl.TEXTURE_2D, null);
            picture = { texture: upload, width, height };
          }
          target.clearTo(0, 0, 0, 0);
          const ok = run.ProcessOpenGL(picture, target.fbo, [0, 0, width, height]);
          return {
            ok,
            pixels: host === 'rgba8' ? readBytes(target.fbo, width, height) : readCodes(target.fbo, width, height),
            groups: run.plan ? run.plan.groups.map((g) => ({ factor: g.factor, scales: g.scale.slice(0, g.count) })) : [],
            truncated: run.truncated,
            buffersFailed: run.buffersFailed,
            error: gl.getError(),
          };
        },
        end() {
          run.DeInitGL();
          target.dispose();
          gl.deleteTexture(upload);
        },
      };
    },

    /** Uniforms the page set that a program lacks, and uniforms a program has that nothing set. */
    uniforms() {
      return {
        missing: Object.entries(programs).flatMap(([k, p]) => [...p.missing].map((u) => `${k}: ${u}`)),
        unset: Object.entries(programs).flatMap(([k, p]) => unsetUniforms(p).map((u) => `${k}: ${u}`)),
      };
    },

    /** A clip of the page's own (the harness's card by default), RGBA8, top row first. */
    clip(id, width, height, time = 0) {
      const source = SOURCES.find((s) => s.id === id);
      if (!source) throw new Error(`no clip '${id}'`);
      clips.render(source, width, height, time);
      return readBytes(clips.buffer.fbo, width, height);
    },

    /**
     * The crosscheck's --cpp-weights: a material's table from weights fitted
     * elsewhere (the C++'s), in place of the page's own fit. Never called by the
     * page itself.
     */
    useWeights(material, weights, worstL1) {
      if (!Array.isArray(weights) || weights.length !== TableFor(material).weights.length) {
        throw new Error(`useWeights(${material}): ${weights?.length} weights, not ${TableFor(material).weights.length}`);
      }
      const ours = TableFor(material);
      SetTable(material, { weights: Float64Array.from(weights), worstL1: worstL1 ?? ours.worstL1, worstL1At: ours.worstL1At });
    },

    /** The page's fits: each material's state, how long it took and where it ran. */
    fits() {
      return { state: [...fits.state], ms: [...fits.ms], where: fits.where };
    },
  };
}

//===========================================================================
// The page.
//===========================================================================

const REPO = 'https://github.com/stoatworks-labs/lithophane';
const page = { mounted: null, instance: null, status: '', fits };

function describe(instance) {
  const plan = instance.plan;
  const material = MaterialAt(plan.material);
  const parts = [`${material.name}, ${plan.dThin.toFixed(2)}–${plan.dThick.toFixed(2)} mm`, `${(plan.pitch * 1000).toFixed(1)} µm a pixel`];
  const byFactor = plan.groups.map((g) => `${g.count} at ${g.factor === 1 ? 'full size' : `1/${g.factor}`}`);
  const scales = plan.groups.reduce((n, g) => n + g.count, 0);
  const deltas = plan.delta.filter(Boolean).length;
  parts.push(`${scales} Gaussian${scales === 1 ? '' : 's'} in ${plan.groups.length} group${plan.groups.length === 1 ? '' : 's'} (${byFactor.join(', ') || 'none'})`
    + (deltas ? `, ${deltas} narrower than a quarter pixel left unblurred` : ''));
  if (instance.truncated) parts.push(`the plan had more than ${kMaxGroups} groups: the widest dropped, as the plugin drops them`);
  const t = TableFor(plan.material);
  const ms = fits.ms[plan.material];
  parts.push(`fit’s worst L1 ${t.worstL1.toFixed(5)}${ms !== null ? `, fitted in ${ms.toFixed(0)} ms (${fits.where})` : ''}`);
  return parts.join(' · ');
}

function createRenderer(gl, quad) {
  const programs = compilePrograms(gl);
  const instance = new Lithophane(gl, programs);
  instance.InitGL();
  page.instance = instance;
  // The kit's clip renderer is private to mountDemo; the crosscheck draws the
  // page's clips through one of its own, on the same context and quad.
  installHook(gl, programs, new SourceRenderer(gl, quad));

  return {
    render({ input, params }) {
      // The host restates every parameter each frame.
      for (const p of PARAMS) instance.SetFloatParameter(p.slot, params.get(p.id));
      const material = MaterialFromParam(instance.params[PT.MATERIAL]);
      if (!HasTable(material)) {
        // Nothing is drawn until this material's table is fitted: the kit has
        // cleared the canvas, and the line under it says why.
        wantFit(material);
        page.status = fits.state[material] === 'failed'
          ? `The fit of ${kMaterialNames[material]} failed; see the browser’s console.`
          : `Fitting ${kMaterialNames[material]}’s table (15 Gaussians, 111 thicknesses), as the plugin does the first time a material is chosen…`;
        return;
      }
      instance.ProcessOpenGL(input, null);
      page.status = describe(instance);
    },
  };
}

const COMMON = {
  es: 'Precision. The kit gives every fragment stage highp float, int and samplers; the plugin’s shaders need nothing else (every read is a texelFetch, so no float filtering), and the page adds nothing to their text (demo/tools/check_shaders.py pins it). A desktop GPU runs every precision at 32 bits either way. This page was checked on a desktop GPU only: on a phone it is untested.',
  presets: 'The presets are the page’s. The plugin declares none; each is only a combination of the plugin’s own parameters.',
  about: 'The About block — a text line and link buttons for the host’s panel — is absent: a web page has links of its own. So are the plugin’s log file and its harness’s negative controls (every model uniform is set as shipped: AttenuationModel 0, Premultiply 1, ClampOn 1 and the rest 0).',
};

// Every material is fitted once, in the background, so that choosing one is
// quick; the one on screen goes first (wantFit moves it to the front).
startWorker();
fits.queue = kMaterialNames.map((_, m) => m);

const mounted = mountDemo({
  name: 'SW Lithophane',
  pluginId: 'LP01',
  tagline:
    'Your clip carved into a backlit porcelain plate: thick where the picture is dark, thin where it is light. Each channel is absorbed on the way through, blue most, so a white lamp comes through warm and redder where the plate is thick; and the light scatters, so every point of the lamp comes out as a soft spot that grows with the thickness it crossed. Light it from the front and the plate is a white relief. Nothing is drawn: it all follows from the thickness.',
  repo: REPO,
  blurb:
    'It is SW Lithophane’s own GLSL — its carve, spread, pass and composite shaders, ported from the repository to WebGL2 unedited — run by a JavaScript port of the plugin’s CPU half, checked by a reader and by a crosscheck against the plugin’s harness, not by the harness itself: the materials, the multipole diffusion profile, the 15-Gaussian fit (made in this page, in a Worker), the kernels, the frame’s plan and every conversion. The clip is the plugin harness’s own card, one of the kit’s generated clips, or a picture of your own.',
  kind: 'effect',
  sources: ['card', 'scene', 'spot', 'ramp', 'bars', 'grid', 'detail', 'alpha'],
  params: PARAMS,
  // The plugin declares no presets. These are the page's: combinations of
  // the plugin's own parameters and nothing else.
  presets: {
    'Feldspar Porcelain': { material: 1 },
    'Clay Porcelain: more scattering': { material: 2 },
    'White PLA: a printed lithophane': { material: 3 },
    'Linear Light: Cura’s mapping': { mapping: Mapping.LinearLight },
    'A thick plate, 0.8 to 6 mm': { thickest: ParamFromThickest(6.0) },
    'A thin plate, 0.5 to 1.5 mm': { thinnest: ParamFromThinnest(0.5), thickest: ParamFromThickest(1.5) },
    'A 20 mm plate: the blur spans many pixels': { plateWidth: ParamFromPlateWidth(20.0) },
    'A 400 mm plate': { plateWidth: ParamFromPlateWidth(400.0) },
    'Front lit: the lamp behind off': { backLight: 0, frontLight: ParamFromFrontLight(1.0) },
    'Front lit, raking from the left at 15°': { backLight: 0, frontLight: ParamFromFrontLight(1.0), lightAngle: ParamFromLightAngle(180.0), lightElevation: ParamFromLightElevation(15.0) },
    'Front lit, the relief facing the lamp': { backLight: 0, frontLight: ParamFromFrontLight(1.0), reliefSide: ReliefSide.FacingLamp },
    'Back Light 4: a dimmer lamp': { backLight: ParamFromBackLight(4.0) },
    'Mix 0.5: half the clip, half the plate': { mix: 0.5 },
  },
  needFloat: true,
  showBackdrop: true,
  differences: [
    'The CPU half is a port, checked by a reader and by a crosscheck, not by the plugin’s own harness. The five shaders are the plugin’s, unedited. What runs them is translated to JavaScript by hand: demo/plate.js is Plate.cpp (the four materials, the multipole’s parameters, the slab’s radial profile and its variance, the 15-Gaussian non-negative least-squares fit on 111 thickness rows, the kernels with their radius fixed and σ bisected, and the frame’s plan: which scales are active, which are narrower than a quarter pixel, each scale’s decimation factor, the groups of up to four, the weight table as uploaded) and Controls.cpp (the table of controls and every conversion); demo/port.js is Lithophane.cpp’s frame (every pass, uniform and texture unit, in the plugin’s order) and MultiTarget. demo/tools/crosscheck/port_numbers.py compiled the plugin’s own Plate.cpp and Controls.cpp into a printer and compared 3,376,493 numbers with the port’s on 396 plans: the plans, the kernels and every uniform handed to the GPU were identical to the bit, and the fit’s weights and the uploaded table agreed to within 7.4e-8 and 2.4e-7 (JavaScript’s exp and log are not Apple’s, and the fit’s normal equations carry their last bits into the small weights). demo/tools/crosscheck/crosscheck.py ran the page and lptest, the plugin’s harness driving the plugin itself, on the same frames: see the repository’s AGENTS.md for its figures. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shaders drifts, or if the C++ the port was made from changes.',
    'The fit is made in this page. The plugin fits a material’s weight table the first time a frame needs it; so does this page, in a module Worker (the material on screen first, then the rest), and nothing is drawn until that material’s table is back — the line under the picture says “Fitting” meanwhile. There is no precomputed table. If the Worker cannot start, the fit runs on the page’s own thread and the line says so.',
    'Alpha. The kit’s clips and a picture of your own are premultiplied, as the kit takes Resolume’s clips to be. The plugin treats a clip’s colour as straight alpha (its README lists that as not established), so over the Shape on transparency clip a half-transparent edge is carved as if its colour were darker than it is. A transparent pixel is the plate’s thickest porcelain, not a hole: at Mix 1 the output is opaque, so the backdrop shows only with Mix below 1. Every other clip is opaque.',
    'The default clip is the plugin harness’s own card (lptest’s buildCard), ported to a small GLSL shader of the page’s own in float, where lptest builds it in double; the crosscheck counts the pixels where the two differ. The kit’s clips are not photographs of anything carved: here, as in the plugin, thickness is the clip’s luma, Thickest for black and Thinnest for white, which is the lithophane maker’s convention, not a measurement.',
    'Scale. The plate’s scattering is in millimetres and Plate Width says how many millimetres the frame is across, so the same plate blurs over more pixels at 1920 × 1080 than at 640 × 360, as the plugin’s does. Changing the size makes a new plan, as the plugin does; the plan keeps no state from the last raster.',
    'At most eight groups of blur passes. The composite reads eight; a plan with more drops the widest, as the plugin does (it logs it; here the line under the picture says it). No setting on this page’s sizes reaches eight.',
    COMMON.es,
    'The line under the picture is not the plugin. It reports what the port computed for this frame — the material and the plate’s range, the pitch, the Gaussians and their groups, the fit’s worst L1 and how long the fit took here — and the plugin draws no such thing.',
    COMMON.presets,
    COMMON.about,
    'What the harness measured, and this page did not: each channel’s transmission falls as exp(−k d) with the measured k to four decimals; a one-pixel line’s blur radius equals the multipole model’s prediction to the third decimal; the relief shades by Lambert’s law to 1e-6; 16 deliberately wrong models and 54 one-character mutants of the shipped code are all caught. That is tools/lptest in the repository, and it, not this page, is the reason to believe the plate. When this page was made no host had loaded the plugin, not Arena and not Avenue; its README says what has been checked since.',
  ],
  createRenderer,
});

page.mounted = mounted ?? null;

// When a fit comes back: draw again (the kit only redraws by itself while playing).
fits.onDone = () => page.mounted?.redraw();

// The line under the picture.
const statLine = document.createElement('p');
statLine.className = 'stage__status lp-status';
statLine.setAttribute('aria-live', 'off');
statLine.dataset.role = 'lithophane-stats';
document.querySelector('.stage')?.append(statLine);
if (statLine.isConnected) {
  const tick = () => {
    if (statLine.textContent !== page.status) statLine.textContent = page.status;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

// For a headless check: the mounted page.
window.lithophanePage = page;
