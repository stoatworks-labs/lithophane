/**
 * The page's fit, off the main thread. The plugin fits a material's weight
 * table (Plate.cpp: 15 Gaussians by non-negative least squares on 111
 * thickness rows) the first time a frame needs that material, on the host's
 * render thread; a fit takes a tenth of a second or so, which a browser's
 * main thread would show as a stall. This runs demo/plate.js's own fit, the
 * same code the page would run, and hands the table back whole: its weights
 * are a Float64Array, and a structured clone keeps every bit.
 *
 * In: { material }. Out: { material, table, ms } or { material, error }.
 */
import { TableFor, MaterialAt } from './plate.js';

self.onmessage = (event) => {
  const material = event.data?.material | 0;
  try {
    const t0 = performance.now();
    const table = TableFor(material);
    const ms = performance.now() - t0;
    self.postMessage({ material, name: MaterialAt(material).name, table, ms });
  } catch (error) {
    self.postMessage({ material, error: String(error?.message ?? error) });
  }
};
