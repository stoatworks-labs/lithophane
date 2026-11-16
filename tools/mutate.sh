#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the physics, would pass every mutant here.
#
# One copy of the tree in a temporary directory (the FFGL SDK is symlinked,
# not copied), built arm64-only once. Each mutant edits its file in the copy,
# deletes that file's object -- every object, for a header -- and the binary
# (make compares mtimes to the second, so an edit within the same second as
# the last build would otherwise be missed, and a relink skipped would run the
# last mutant's lptest), builds lptest, requires a GLSL mutant to be in the
# binary verbatim, runs the named check at 320x180 (or the raster it names:
# '--blur@1280x720') under a time limit (a
# mutant can hang: honeydew's lesson; a timeout counts as caught), then puts
# the file back. --resize always runs 1280x720 -> 320x180 and the offline
# --model ignores --size. (ferrofluid's script by way of fountain, filings,
# intensifier and convergence; lithophane's mutants.)
#
#     tools/mutate.sh          every mutant
#     ONLY=3 tools/mutate.sh   the third alone (ONLY=3,7 the third and seventh)
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
#
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap): no GLSL || here, which is why the spread's and pass's
# edge-model lines have no mutant. Mutants left out as equivalent are in
# AGENTS.md.
MUTANTS=(
	"source/Shaders.cpp|/ 1.055, vec3( 2.4 ) );|/ 1.055, vec3( 2.5 ) );|--attenuation|GLSL: the sRGB decode's exponent 2.5, 4 -> 5"
	"source/Shaders.cpp|vec3( 1.0 / 2.4 ) ) - 0.055;|vec3( 1.0 / 2.4 ) ) - 0.065;|--attenuation|GLSL: the sRGB encode's offset 0.065, 5 -> 6"
	"source/Shaders.cpp|		lin *= src.a;|		lin /= src.a;|--alpha|GLSL: the clip's light divided by its alpha, * -> /"
	"source/Shaders.cpp|vec4( 0.0 ), isNaN );|vec4( 1.0 ), isNaN );|--robust|GLSL: a NaN read as 1, not 0, 0 -> 1"
	"source/Shaders.cpp|vec3( 0.299, 0.587, 0.114 ) ) ) * Span;|vec3( 0.299, 0.687, 0.114 ) ) ) * Span;|--attenuation|GLSL: Linear Depth's luma weights green 0.687, 5 -> 6"
	"source/Shaders.cpp|d         = Thin - log(|d         = Thin + log(|--attenuation|GLSL: Linear Light's thickness the wrong way, - -> +"
	"source/Shaders.cpp|pow( code, vec3( 2.2 ) )|pow( code, vec3( 2.3 ) )|--attenuation|GLSL: Cura's luminance gamma 2.3, 2 -> 3"
	"source/Shaders.cpp|: exp( -k * d );|: exp( +k * d );|--attenuation|GLSL: the column grows brighter with thickness, - -> +"
	"source/Shaders.cpp|vec3 k = AttenuationModel == 1 ?|vec3 k = AttenuationModel == 0 ?|--attenuation|GLSL: red and blue swapped in the plugin, 1 -> 0"
	"source/Shaders.cpp|d = clamp( d, Thin, Thin + Span );|d = clamp( d, Thin, Thin * Span );|--attenuation|GLSL: the thickest clamped at Thin x Span, + -> *"
	"source/Shaders.cpp|float x = ( d - RowOrigin ) / RowStep;|float x = ( d + RowOrigin ) / RowStep;|--blur|GLSL: the weight table read a millimetre off, - -> +"
	"source/Shaders.cpp|return lo + t * ( hi - lo );|return lo - t * ( hi - lo );|--blur|GLSL: the table's rows interpolated backwards, + -> -"
	"source/Shaders.cpp|return 0.75 - u * u;|return 0.76 - u * u;|--blur|GLSL: the B-spline's centre 0.76, 5 -> 6"
	"source/Shaders.cpp|return 0.5 * ( u - 1.5 ) * ( u - 1.5 );|return 0.5 * ( u - 1.6 ) * ( u - 1.5 );|--blur|GLSL: the B-spline's side lobe off, 5 -> 6"
	"source/Shaders.cpp|( at.x - 3 ) * Factor;|( at.x - 2 ) * Factor;|--blur|GLSL: the decimation along x a texel off, 3 -> 2"
	"source/Shaders.cpp|( along - 3 ) * Factor;|( along - 2 ) * Factor;|--blur|GLSL: the decimation along y a texel off, 3 -> 2"
	"source/Shaders.cpp|weightAt( Column.x, row, t );|weightAt( Column.y, row, t );|--blur|GLSL: the first scale given the second's weight, x -> y"
	"source/Shaders.cpp|( s0 + s1 ) + ( s2 + s3 ), 0.0 );|( s0 + s1 ) + ( s2 - s3 ), 0.0 );|--blur|GLSL: a group's fourth scale subtracted, + -> -"
	"source/Shaders.cpp|- 0.5 * ( ff - 1.0 ) ) / ff;|- 0.6 * ( ff - 1.0 ) ) / ff;|--blur|GLSL: the reconstruction's texel centres off, 5 -> 6"
	"source/Shaders.cpp|ivec2 b  = ivec2( bn ) + 2;|ivec2 b  = ivec2( bn ) + 3;|--blur|GLSL: the reconstruction a low-res texel off, 2 -> 3"
	"source/Shaders.cpp|bspline( tc.x - bn.x + 1.0 )|bspline( tc.x - bn.x + 2.0 )|--blur|GLSL: the reconstruction's left weight from the wrong lobe, 1 -> 2"
	"source/Shaders.cpp|int along  = Direction.x == 1 ? at.x : at.y;|int along  = Direction.x == 0 ? at.x : at.y;|--blur|GLSL: the passes along the wrong axis, 1 -> 0"
	"source/Shaders.cpp|vec3 lin = BackLight * back;|vec3 lin = BackLight / back;|--attenuation|GLSL: Back Light divided by the transmission, * -> /"
	"source/Shaders.cpp|back += level( Level3, LevelFactor[ 3 ], at );|back -= level( Level3, LevelFactor[ 3 ], at );|--blur|GLSL: the fourth group subtracted, + -> -"
	"source/Shaders.cpp|vec2 span = vec2( max( hi - lo, ivec2( 1 ) ) ) * Pitch;|vec2 span = vec2( max( hi - lo, ivec2( 1 ) ) ) / Pitch;|--front|GLSL: the slope in mm times pixels, * -> /"
	"source/Shaders.cpp|max( hi - lo, ivec2( 1 ) ) )|max( hi - lo, ivec2( 2 ) ) )|--edges|GLSL: a central difference's span at the frame's edge, 1 -> 2"
	"source/Shaders.cpp|-g, 1.0 ) );|-g, 2.0 ) );|--front|GLSL: the normal's z doubled, 1 -> 2"
	"source/Shaders.cpp|max( dot( n, LightDir ), 0.0 )|max( dot( n, LightDir ), 1.0 )|--front|GLSL: Lambert's cosine floored at 1, 0 -> 1"
	"source/Shaders.cpp|return 0.06 + 0.94 * 0.5 * R|return 0.07 + 0.94 * 0.5 * R|--front|GLSL: the surface reflectance r1 0.07, 6 -> 7"
	"source/Shaders.cpp|( KmK + 2.0 * KmS ) );|( KmK + 3.0 * KmS ) );|--front|GLSL: Kubelka-Munk's kappa with 3 S, 2 -> 3"
	"source/Shaders.cpp|kappa / tanh( kappa * d )|kappa * tanh( kappa * d )|--front|GLSL: Kubelka-Munk's coth as tanh, / -> *"
	"source/Shaders.cpp|AlphaModel == 1 ? a : 1.0;|AlphaModel == 1 ? a : 0.0;|--alpha|GLSL: the plate transparent, 1 -> 0"
	"source/Shaders.cpp|src.rgb * a * ( 1.0 - m )|src.rgb * a * ( 2.0 - m )|--alpha|GLSL: the clip's share at Mix m 2 - m, 1 -> 2"
	"source/Shaders.cpp|if( MixAmount <= 0.0 )|if( MixAmount <= 0.5 )|--alpha|GLSL: the clip handed through up to Mix 0.5, 0 -> 5"
	"source/Plate.cpp|4.0 * k * k / 3.0|4.0 * k * k / 2.0|--model|C++: sigma_a from k with 4 k^2 / 2, 3 -> 2"
	"source/Plate.cpp|const double A = ( 1.0 + m.Fdr )|const double A = ( 2.0 + m.Fdr )|--blur|C++: the boundary's A with 2 + F_dr, 1 -> 2"
	"source/Plate.cpp|const double t   = h * ( 1.0 + p.sigtr * rho )|const double t   = h * ( 2.0 + p.sigtr * rho )|--model|C++: the dipole's near term doubled, 1 -> 2"
	"source/Plate.cpp|M2 += sg * p.alpha * h * e / p.sigtr;|M2 += sg * p.alpha * h * e * p.sigtr;|--model|C++: the closed-form second moment times sigma_tr, / -> *"
	"source/Plate.cpp|constexpr double kHold = 100.0;|constexpr double kHold = 000.0;|--model|C++: the fit's variance not held, 1 -> 0"
	"source/Plate.cpp|return kScale0 * std::pow( 2.0, 0.5 * j );|return kScale0 * std::pow( 2.0, 0.6 * j );|--blur|C++: the scales a factor 2^0.6 apart, 5 -> 6"
	"source/Plate.cpp|std::ceil( 4.0 * std::sqrt( variance ) )|std::ceil( 1.0 * std::sqrt( variance ) )|--model|C++: the kernels cut at one sigma, 4 -> 1"
	"source/Plate.cpp|std::log2( plan.sigmaPx[ j ] / 2.0 )|std::log2( plan.sigmaPx[ j ] / 1.0 )|--model|C++: the factor rule sigma / f >= 1, 2 -> 1"
	"source/Plate.cpp|/ ( double( f ) * f ) - 0.5;|/ ( double( f ) * f ) - 0.6;|--blur|C++: the low-res variance's B-spline allowance 0.6, 5 -> 6"
	"source/Plate.h|constexpr double kDeltaBelowPx = 0.25;|constexpr double kDeltaBelowPx = 0.75;|--model|C++: the delta column under three quarters of a pixel, 2 -> 7"
	"source/Plate.cpp|{ 0.90, 1.01, 1.42 }|{ 0.90, 1.02, 1.42 }|--attenuation|C++: Porcelain's green k 1.02, 1 -> 2"
	"source/Plate.cpp|m.S[ c ] = 0.75 * m.musp[ c ];|m.S[ c ] = 0.85 * m.musp[ c ];|--model|C++: PLA's S = 0.85 mu_s', 7 -> 8"
	"source/Plate.cpp|const double r1 = 0.06, r2 = 0.5;|const double r1 = 0.06, r2 = 0.6;|--model|C++: the K-M inner reflectance r2 0.6, 5 -> 6"
	"source/Lithophane.cpp|std::exp( -mat.k[ 1 ] * span )|std::exp( -mat.k[ 2 ] * span )|--attenuation|C++: Linear Light's MinT from blue's k, 1 -> 2"
	"source/Lithophane.cpp|PT_LIGHT_ELEVATION ] ) * kPi / 180.0;|PT_LIGHT_ELEVATION ] ) * kPi / 190.0;|--front|C++: the elevation in the wrong unit, 8 -> 9"
	"source/Lithophane.cpp|std::memcmp( key, planKey, sizeof( key ) ) != 0|std::memcmp( key, planKey, sizeof( key ) ) == 0|--resize|C++: the plan kept while its inputs change, ! -> ="
	"source/Lithophane.cpp|hooks.decimationModel == 1 ? 2 : 1;|hooks.decimationModel == 1 ? 2 : 2;|--blur|C++: the box decimation in the plugin, 1 -> 2"
	"source/GLState.h|viewport[ 2 ], viewport[ 3 ] );|viewport[ 2 ], viewport[ 2 ] );|--glstate|C++: the host's viewport put back square, 3 -> 2"
	"source/Controls.h|constexpr double kFrontLightHigh = 2.0;|constexpr double kFrontLightHigh = 3.0;|--model|C++: Front Light's top 3, 2 -> 3"
	"source/Controls.h|kThickestHigh = 6.0;|kThickestHigh = 7.0;|--model|C++: Thickest's top 7 mm, 6 -> 7"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DLITHOPHANE_BUILD_TOOLS=ON >/dev/null
cmake --build "$tree/build" --target lptest -j6 >/dev/null 2>&1 || { echo "the unmutated copy does not build"; exit 1; }

caught=0
ran=0
index=0
for entry in "${MUTANTS[@]}"; do
	index=$(( index + 1 ))
	[[ -n "${ONLY:-}" && ",$ONLY," != *",$index,"* ]] && continue
	ran=$(( ran + 1 ))
	IFS='|' read -r file original mutant check what <<<"$entry"
	cp "$REPO/$file" "$tree/$file"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	# A header's mutant reaches every object that includes it: delete them all.
	object="$( basename "$file" ).o"
	[[ "$file" == *.h ]] && object="*.o"
	find "$tree/build" -name "$object" -delete
	# The binary too: a relink skipped would run the last mutant's lptest.
	rm -f "$tree/build/lptest"
	printf '\n== mutant %d: %s\n' "$index" "$what"
	status=0
	if ! cmake --build "$tree/build" --target lptest -j6 >"$WORK/build.log" 2>&1; then
		# A mutant that does not compile proves nothing about the checks.
		printf '   FAIL  the mutant does not build -- choose another\n'
		tail -5 "$WORK/build.log" | sed 's/^/        /'
	elif [[ "$file" == source/Shaders.cpp ]] && ! LC_ALL=C grep -qF -- "$mutant" "$tree/build/lptest"; then
		# A GLSL mutant is a string literal: it must be in the binary, verbatim.
		printf '   FAIL  the mutant is not in the built binary -- the build did not pick it up\n'
	else
		size=320x180
		if [[ "$check" == *@* ]]; then
			size="${check#*@}"
			check="${check%@*}"
		fi
		limit 600 "$tree/build/lptest" "$check" --size "$size" >"$WORK/log" 2>&1 || status=$?
		if [[ "$status" -eq 0 ]]; then
			printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
		else
			printf '   ok    %s fails against the mutant (exit %d%s):\n' "$check" "$status" "$( [[ "$status" -eq 142 ]] && echo ', the time limit' )"
			# A mutant that timed out printed no FAIL: grep finding none must not end the run.
			{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
			caught=$(( caught + 1 ))
		fi
	fi
	cp "$REPO/$file" "$tree/$file"
	find "$tree/build" -name "$object" -delete
done

printf '\nmutants: %d, caught: %d\n' "$ran" "$caught"
[[ "$ran" -gt 0 && "$caught" -eq "$ran" ]]
