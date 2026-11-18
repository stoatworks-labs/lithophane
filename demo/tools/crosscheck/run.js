// The page's half of the crosscheck: fetch the cases and the harness's card
// frames from crosscheck.py's server, run each case through a fresh instance
// (window.lithophaneDemo.begin), frame by frame with the cues the job gives,
// and post every frame back. A case on one of the kit's clips draws the clip
// in the page (window.lithophaneDemo.clip) and posts those bytes too, so the
// harness can be handed exactly what the page was. Also posted: the page's
// table of controls, the page's own card for comparison with the harness's,
// and any uniform the page set that a program lacks or a program has that
// nothing set. crosscheck.py compares; nothing is judged here.
const post = (path, body) => fetch(path, { method: 'POST', body });

async function main() {
  for (let i = 0; i < 400 && !window.lithophaneDemo; i += 1) await new Promise((r) => setTimeout(r, 50));
  if (!window.lithophaneDemo) throw new Error('window.lithophaneDemo never appeared: the page did not start');
  const demo = window.lithophaneDemo;
  // The page fits every material in its Worker at load. Let it finish, so a
  // table handed in below is not replaced by the Worker's afterwards.
  for (let i = 0; i < 600 && demo.fits().state.some((s) => s !== 'done' && s !== 'failed'); i += 1) {
    await new Promise((r) => setTimeout(r, 50));
  }
  const job = await (await fetch('/xc/job.json')).json();
  let weights = 'its own fit, made in the page (' + demo.fits().where + ')';
  if (job.weights) {
    for (let m = 0; m < 4; m += 1) demo.useWeights(m, job.weights[String(m)], job.weights[`${m}.worstL1`]);
    weights = "the C++'s (--cpp-weights)";
  }
  const sequences = {};
  const sequence = async (key) => {
    if (!sequences[key]) sequences[key] = new Uint8Array(await (await fetch(`/xc/frames/${key}`)).arrayBuffer());
    return sequences[key];
  };
  await post('/xc/table', JSON.stringify(demo.table));
  const errors = [];
  const refused = [];
  const truncated = [];
  const groups = {};
  for (const c of job.cases) {
    const size = c.width * c.height * 4;
    const frames = c.clip === 'card' ? await sequence(c.cards) : null;
    const out = new Uint8Array(size * c.frames);
    const clipBytes = c.clip === 'card' ? null : new Uint8Array(size * c.frames);
    const run = demo.begin({ settings: c.settings, width: c.width, height: c.height, input: job.input });
    try {
      for (let n = 0; n < c.frames; n += 1) {
        const index = c.indices[n];
        let picture;
        if (frames) {
          picture = frames.subarray(index * size, (index + 1) * size);
        } else {
          picture = demo.clip(c.clip, c.width, c.height, c.times[n]);
          clipBytes.set(picture, n * size);
        }
        const r = run.frame(picture, 0, c.changes[n] ?? {});
        out.set(r.pixels, n * size);
        if (r.error) errors.push(`${c.name} frame ${n}: GL error ${r.error}`);
        if (!r.ok || r.buffersFailed) refused.push(`${c.name} frame ${n}`);
        if (r.truncated) truncated.push(`${c.name} frame ${n}`);
        if (n === c.frames - 1) groups[c.name] = r.groups;
      }
    } finally {
      run.end();
    }
    await post(`/xc/out/${c.name}`, out);
    if (clipBytes) await post(`/xc/clip/${c.name}`, clipBytes);
  }
  for (const k of job.cards) {
    await post(`/xc/out/card-${k.name}`, demo.clip('card', k.width, k.height, 0));
  }
  await post('/xc/missing', JSON.stringify({ ...demo.uniforms(), errors, refused, truncated, groups, weights }));
  await post('/xc/done', 'ok');
}

main().catch((error) => post('/xc/error', String(error?.stack ?? error)));
