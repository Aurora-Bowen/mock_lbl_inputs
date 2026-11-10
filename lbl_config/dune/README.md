# DUNE inputs

Status: work in progress on branch `dune-genie-ar23`.

## Source

`tdr2017/` holds the ancillary files of arXiv:2103.04797v2, downloaded
2026-11-09 from arxiv.org. The paper states the fluxes were produced with
G4LBNF v3r5p4 for the optimized engineered beam design used in the DUNE TDR.
The same version string (`v3r5p4`, `OptimizedEngineeredNov2017`) appears in the
GLoBES flux file names in `tdr2017/globes_config/flux/`, which are
byte-identical to the FD `*_globes.txt` files in `tdr2017/flux/`.

Checksums of every file are in `tdr2017/SHA256SUMS.txt` (paths relative to `tdr2017/`).

## Layout

    tdr2017/flux/            flux_dune_{neutrino,antineutrino}_{ND,FD}.root
                             flux_dune_{neutrino,antineutrino}_{ND,FD}_globes.txt
    tdr2017/globes_config/   GLoBES configuration tree, unmodified
    tdr2017/SHA256SUMS.txt

## Flux ROOT histograms

- Bin content is the per-bin integrated flux in nu/m^2/POT, with variable bin
  width: 0.125 GeV up to 8 GeV, 0.5 GeV up to 20 GeV, 2 GeV up to 118 GeV.
  Verified by comparing the plain sum of `numu_flux` in
  `flux_dune_neutrino_FD.root` with the integral of the numu column of the
  matching GLoBES flux file (ratio 1.000000). Do not multiply by bin width.
- Only the `*_flux` histograms are used. `*_fluxosc` and `*_eventrate*` are not
  used because their oscillation parameters and units are not documented.
- Flux locations: near detector (ND) 574 m and far detector (FD) 1297 km
  downstream of the start of Horn 1.

## GLoBES-format flux text files (`*_globes.txt`)

Units nu/GeV/m^2/POT, 7 columns: energy at bin centre, nue, numu, nutau,
nuebar, numubar, nutaubar; 0.25 GeV steps.

## globes_config/

Kept unmodified as the source of the far-detector smearing matrices and
post-smearing efficiencies. GLoBES itself is not used by this repository.

## Citation

DUNE requests that these files be cited as arXiv:2103.04797 and
Eur. Phys. J. C 80, 978 (2020), arXiv:2006.16043.

## Sept 2021 flux (sept2021/)

Status: added for comparison; the choice of production flux (TDR Nov 2017 or
Sept 2021) is pending.

### Source

- Page: https://glaucus.crc.nd.edu/DUNEFluxes/, entry "Optimized 3-horn Design
  with 1.5 m target and fully engineered Horns (Sept 2021)". The page states
  120 GeV protons and 1.1e21 POT per year at 1.2 MW.
- The 4 files were downloaded manually from the page's Box links on 2026-11-09
  and are kept unmodified with their original names in `sept2021/flux/`.
  Checksums: `sept2021/SHA256SUMS.txt` (paths relative to `sept2021/`).
- Generator version `v3r5p10` and physics list `QGSP_BERT`, as given in the
  file names (`histos_g4lbne_v3r5p10_QGSP_BERT_OfficialEngDesignSept2021_*`).
- Each file has a `MC_Parameters` tree with potperfile = 1000000, nfiles = 101,
  total_pot = 101000000. It has no detector-location field.

### File mapping

| file (`sept2021/flux/histos_g4lbne_v3r5p10_QGSP_BERT_OfficialEngDesignSept2021_...`) | mode | detector | TDR counterpart (`tdr2017/flux/`) |
|---|---|---|---|
| `..._neutrino_DUNEFD_fastmc.root`     | FHC | FD | `flux_dune_neutrino_FD.root` |
| `..._antineutrino_DUNEFD_fastmc.root` | RHC | FD | `flux_dune_antineutrino_FD.root` |
| `..._neutrino_DUNEND_fastmc.root`     | FHC | ND | `flux_dune_neutrino_ND.root` |
| `..._antineutrino_DUNEND_fastmc.root` | RHC | ND | `flux_dune_antineutrino_ND.root` |

The files have the same 36 histogram names, axis titles and binning as the TDR
files (137 bins over 0-118 GeV; width 0.125 GeV up to 8 GeV, 0.5 GeV up to 20 GeV,
2 GeV up to 118 GeV). Only the `*_flux` histograms are used, as for the TDR files.

### Units

`*_flux` bin content is the per-bin integrated flux in nu/m^2/POT, the same
convention as the TDR files (no GLoBES file exists for this flux, so this was
established from the ROOT files alone):

- at the 8 and 20 GeV width changes the bin content jumps by about the width
  ratio (4) while content/width stays continuous, as in the TDR files;
- the bin-by-bin ratio Sept2021/TDR does not jump by the width ratio at those
  energies;
- the y-axis title is "Unosc ... / m^{2} / POT", and the summed FD FHC
  `numu_flux` is 1.529036e-10, against 1.547952e-10 for TDR (ratio 0.988).

### Locations

The documentation page states that histogram files typically contain ND at
574 m and FD at 1297 km downstream of the start of Horn 1. The files do not
record the location, and nothing more is claimed here for this design.

### Citation

The page's FAQ asks users to cite the DUNE TDR Physics Volume
(arXiv:2002.03005) and the webpage https://glaucus.crc.nd.edu/DUNEFluxes/.

## Far-detector response (tdr2017_response.root)

`tdr2017/tdr2017_response.root` holds the TDR far-detector response from
`tdr2017/globes_config/` (top file `DUNE_GLoBES.glb` and everything it includes)
in a layout that does not need GLoBES. It is produced by
`scripts/dune/convert_response.sh` (runs `src/dune/ConvertGlobesResponse.C` from
the repository root), which reads the AEDL files as plain text. The same run
writes `tdr2017/channels.tsv` (one row per GLoBES channel) and `tdr2017/rules.tsv`
(one row per (rule, channel) term), each with the `file:line` source of every
row. No flux is stored here: the flux stays in the `flux/` folders, so the
response does not depend on the flux version.

### Object layout

| path | type | content |
|---|---|---|
| `smearing/<energy_name>` (16) | TH2D, x: 99 true-energy sampling bins, y: 80 reconstructed-energy bins | matrix element (reco bin i, sampling point k) exactly as in `smr/<name>.txt`; bins outside the row's k_low..k_high are 0 |
| `efficiency/<FHC\|RHC>/<channel>` (16 + 16) | TH1D, 80 reconstructed-energy bins | the `@post_smearing_efficiencies` list of that channel |
| `xsec/<CC\|NC>/<flavour>` (2 x 6) | TGraph, 75 points | x = 10^(log10 E) [GeV], y = sigma/E as tabulated in `xsec/xsec_cc.dat` / `xsec_nc.dat`; reference only (see the title) |
| `meta/` | TNamed | `source`, `converter`, `git_commit`, `globes_version`, `sha256` (every input file read, from `tdr2017/SHA256SUMS.txt`), `reco_binning`, `true_binning`, `reco_edges`, `true_edges` |
| `meta/globals/`, `meta/constants/` | TNamed | every `$` setting and every constant from `definitions.inc` (value, source line; for constants, where they are used) |
| `meta/nuflux/`, `meta/cross/`, `meta/rule/`, `meta/sys/` | TNamed | the full text of each flux, cross-section, rule and systematic definition with source lines |

Object names are the GLoBES names without the `#`/`%` prefix (for example
`#app_nue_sig` -> `smearing/app_nue_sig`, channel `#FHC_app_osc_nue` ->
`efficiency/FHC/FHC_app_osc_nue`). The FHC/RHC directory is the beam mode of
the channel's flux definition (`#flux_FHC` or `#flux_RHC`, `flux/Beam.inc:17,24`).
Flavour names in `xsec/` follow the GLoBES column order (nue, numu, nutau,
nuebar, numubar, nutaubar).

### Binning found in the files

- Reconstructed energy: 80 bins from `$emin = 0.0` and `$binsize`
  (`DUNE_GLoBES.glb:32,34`; `$bins` is not set): 64 x 0.125 GeV (0-8),
  2 x 1 GeV (8-10), 5 x 2 GeV (10-20), 9 x 10 GeV (20-110). The widths add up to `$emax = 110.0`.
- True energy: 99 sampling bins from `$sampling_min = 0.0` and
  `$sampling_stepsize` (`DUNE_GLoBES.glb:37,39`): 80 x 0.125 GeV (0-10),
  10 x 1 GeV (10-20), 9 x 10 GeV (20-110). The widths add up to `$sampling_max = 110.0`.
- Every matrix has 80 rows {k_low, k_high, values}, separated by `:` and ended by `;`;
  every row covers k = 0..98 (99 values). All 30 efficiency lists used by channels have 80 values in [0, 1].

### Verification

- The converter prints its consistency checks (row count = bins, values per row =
  k_high - k_low + 1, k range, column sums, efficiency lengths and range,
  cross-section table shape, sha256 coverage); all pass.
- The ROOT file was dumped to text at full double precision and compared,
  value by value, with an independent parser written in Python
  (standard library only, not in this repository): 16 x 8282 matrix bins,
  32 x 82 efficiency bins and 12 x 150 cross-section values agree exactly
  (maximum difference 0); bin edges agree exactly; `channels.tsv` and
  `rules.tsv` agree field by field; the 59 sha256 sums in `meta/sha256` equal
  `SHA256SUMS.txt` and the files.

### Open items

- The AEDL has no polarity or horn-current keyword in the flux definitions.
  FHC/RHC is taken from the definition identifiers `#flux_FHC` / `#flux_RHC`.
  It agrees with the `@time` constants (`NUTIME` / `NUBARTIME`) and with the
  flux tables (the `#flux_FHC` file is numu-dominated, the `#flux_RHC` file
  numubar-dominated, taking the columns in GLoBES order).
- The cross-section column order, the polarity codes `+`/`-`, the flavour codes
  `e`/`m`/`t` and the `NOSC_` prefix follow GLoBES manual conventions; the AEDL
  files themselves do not define them. The nutau and nutaubar columns are zero
  in both tables. Whether sigma/E is per nucleon or per nucleus is not
  established, so `xsec/` is for reference only.
- Column sums of the matrices (sum over reconstructed bins at a fixed sampling point) are 1
  within 1e-5 except: sampling bins with no entries at all (sum 0; 2 to 31 per
  matrix: the lowest sampling bins, up to 3.375 GeV for the tau matrices, and
  the 10 GeV bins above 60-90 GeV; `app_nuebar_bkg` also at 0.875-1 and
  2.125-2.25 GeV); 50-60 and 70-80 GeV in `dis_NC_bkg`, `dis_aNC_bkg`, `app_NC_bkg`,
  `app_aNC_bkg` (0.999986-0.999989); 7.5-7.625 GeV in `app_numubar_bkg` (1.00001).
  The values are stored as they are.
- Name asymmetries in the AEDL, kept as they are: `#FHC_app_bkg_nubarNC` uses
  energy `#app_NC_bkg` (`DUNE_GLoBES.glb:115`) while `#RHC_app_bkg_nubarNC` uses
  `#app_aNC_bkg` (`:169`); both `*_dis_bkg_nubarNC` channels use
  `%post_dis_*_NC_bkg` (`:206`, `:240`). The NC and aNC files of each pair have
  identical values, so this changes no number.
- Not referenced by any include: `eff/post_app_FHC_anuNC_bkg.txt` (defines
  `%app_FHC_anuNC_bkg` with 71 values, not 80), `eff/post_dis_FHC_aNC_bkg.txt`,
  `eff/post_dis_RHC_aNC_bkg.txt`. The lists `%eng` (160 values), `%sbc` and `%bc`
  (`DUNE_GLoBES.glb:58-61`) are defined but not used.
- How GLoBES places a sampling point inside its stepsize interval is GLoBES-internal
  and is not established here; the TH2D x axis uses the stepsize intervals as bins.
- `$filter_state = 0`, `$filter_value`, `$target_mass = LAMASS` (40), the
  `@norm`/`@power`/`@time` of the fluxes and the rules' `@energy_window = 0.5 : 18.0`
  and systematics are recorded in `meta/` only; nothing in the histograms depends on them.

## Production choices

### Far-detector flux

The production FD flux is the TDR (Nov 2017) flux in `tdr2017/flux/`. The
Sept 2021 flux in `sept2021/flux/` is kept for comparison studies.

Reason. A pre-registered rule was tested: within 0.5-8 GeV reconstructed
energy, the composition distance D_flux between the two fluxes must not exceed
the distance D_ref that oscillation-parameter changes already produce
(delta = -pi/2, +pi/2, sin^2 theta23 = 0.4197 against NuFit 4.0 NO), in the bins
holding most signal events. It failed for three of the four signal samples.
Share of signal events in bins with D_flux > D_ref:

| rule | share |
|---|---|
| nue_app | 60 % |
| nuebar_app | 38 % |
| numu_dis | 62 % |
| numubar_dis | 98 % |

The bound on the relative change of the post-smearing signal efficiencies is
nevertheless small: D_flux/epsilon averages 1.6-3.8 % and is at most 5.7 % over
the bins holding 90 % of each signal channel's events. (Work-area report
`reports/05_flux_choice.md`.)

### nu_tau CC backgrounds

nu_tau CC backgrounds are included: nu_tau events are generated with GENIE
AR23 and folded through the nu_tau smearing matrices and post-smearing
efficiencies shipped with arXiv:2103.04797. The GLoBES cross-section files of
that release set the nu_tau and nu_tau-bar columns to zero
(`xsec/CC/nutau`, `xsec/CC/nutaubar` in `tdr2017_response.root`), so the GLoBES
configuration itself produces no nu_tau events. The folding code will have a
switch to exclude them for direct comparison with the GLoBES configuration.

The nu_tau part of the response was checked (all numbers from
`tdr2017_response.root`, `channels.tsv`, `rules.tsv`):

| matrix | first non-zero true-energy column | non-zero columns | column sums above it |
|---|---|---|---|
| `app_nutau_bkg` | 2.625-2.75 GeV | 76 of 99 (up to 80-90 GeV) | 1 within 5e-6; 90-110 GeV empty |
| `dis_nutau_bkg` | 2.625-2.75 GeV | 76 of 99 (up to 80-90 GeV) | 1 within 7e-6; 90-110 GeV empty |
| `app_nutaubar_bkg` | 3.375-3.5 GeV | 68 of 99 (up to 60-70 GeV) | 1 within 5e-6; 70-110 GeV empty |
| `dis_nutaubar_bkg` | 3.375-3.5 GeV | 68 of 99 (up to 60-70 GeV) | 1 within 5e-6; 70-110 GeV empty |

There are 8 nu_tau channels (`final_flavour` = t), each in exactly one rule as a
background with coefficient 1.0: `FHC_app_bkg_nutau(bar)` in `nue_app`,
`RHC_app_bkg_nutau(bar)` in `nuebar_app`, `FHC_dis_bkg_nutau(bar)` in `numu_dis`,
`RHC_dis_bkg_nutau(bar)` in `numubar_dis` (energy window 0.5-18 GeV). Their
post-smearing efficiencies are non-zero over reconstructed energies from
0-0.25 GeV up to 60-100 GeV, with maxima between 0.17 and 1.0. Above the first
non-zero column every column is normalised, so the nu_tau response is usable
above those energies. True nu_tau(bar) energy below 2.625 (3.375) GeV has no
column and would be lost.

## Known response limitations

- Empty true-energy columns (column sum 0): `app_nuebar_bkg` at 0-0.5,
  0.875-1 and 2.125-2.25 GeV (7-12 % of the nuebar flux falls there);
  0-0.125 GeV in the numu/numubar matrices (`app_numu_bkg`, `dis_numu_sig`,
  `app_numubar_bkg`, `dis_numubar_sig`); above 60-90 GeV in most matrices.
- The migrations (smearing matrices) and efficiencies come from the TDR
  simulation with GENIE 2.12.10. Using AR23 changes the rates and the channel
  composition but not the migrations; this matters most for nu_tau.

## Splines

### Production: official GENIE AR23 spline package

The production cross-section splines are the official SciSoft package
`genie_xsec-3.06.00-noarch-AR2320i00000-k250-e1000.tar.bz2`
(https://scisoft.fnal.gov/scisoft/packages/genie_xsec/v3_06_00/, listed 2025-01-19,
448998203 bytes). It contains tune AR23_20i_00_000 with the Default generator list,
250 log-spaced knots from 0.01 to 1000 GeV, and all six flavours.

sha256 `f435a98f5d491a181333f27a9a0b675cb94c326770447b0e98d5190983962dd0`
(recorded at the first download, 2026-11-10; SciSoft publishes no checksum file).

```bash
scripts/dune/get_xsec.sh -d <dir>            # download link taken from the index page
# -> <dir>/genie_xsec/v3_06_00/NULL/AR2320i00000-k250-e1000/data/gxspl-NUsmall.xml
```

The script records and checks the sha256 and refuses to overwrite anything. If
`<dir>` already holds the tarball (for example copied in by hand), it checks the
sha256 and extracts it without downloading. Use `gxspl-NUsmall.xml`, which is the
file the package's own `ups` table points to:

- 51 targets, Ar40 (1000180400) for all of 12, -12, 14, -14, 16 and -16 (102-110
  splines per flavour);
- every spline has 250 knots up to 1000 GeV, and the file declares only tune
  AR23_20i_00_000.

`gxspl-NUbig.xml.gz` has 127 targets and the same Ar40 content.

Compatibility. The package is labelled GENIE 3.06.00; our GENIE is 3.06.02.
Evidence that they are compatible (work-area report `reports/09_official_xsec.md`):

- `gspl2root -p 14 -t 1000180400 --tune AR23_20i_00_000` loads `gxspl-NUsmall.xml`
  with no missing-spline warning and no splines created; it writes 133 graphs for
  nu_mu + Ar40.
- The official `NievesQELCCPXSec/ZExp` numu + O16 (neutron) spline agrees with the
  30 knots computed locally with GENIE 3.06.02. The ratio local/official is
  0.992-1.015 (mean 0.9998) for E >= 0.3 GeV; it is 0.75-1.03 near threshold
  (0.13-0.28 GeV), where the cross section rises steeply and the local value is a
  500-nucleon Monte Carlo average.
- `gevgen` with these splines, tune AR23_20i_00_000, the TDR FD FHC `numu_flux`
  (`,NOWIDTH`, 0-120 GeV) and target Ar40 generated 1000 events in 45 s. It
  computed no splines on the fly: the log has 0 "Creating cross section spline"
  lines, and `GMCJDriver` only summed the loaded splines. Event types: 747 CC,
  253 NC; 185 QE, 60 MEC, 332 RES, 416 DIS, 7 COH. All 1000 events are
  accepted, with error flag 0 and weight 1.

The gevgen log has 30 ERROR lines. None of them comes from spline loading, and
none removes or reweights an event:

| Count | Emitted by | When | Effect |
|---|---|---|---|
| 1 | `QELInteractionListGenerator::CreateInteractionListStrangeCC` | initialisation, numu + Ar40 | The ΔS=1 QE channel exists only for antineutrinos, so the list is empty for numu. No effect. |
| 23 | `KineGeneratorWithCache::AssertXSecLimits`, called from `QELEventGenerator` | one CC QE event, E = 27.4 GeV | Trial kinematics at Q2 ≈ 25-38 GeV2 give a negative dσ. The accept/reject test `xsec_max * rndm < xsec` rejects them. The event is then generated at Q2 = 3.3 GeV2 and kept. |
| 5 | `PDGLibrary::Find`, called from `HAIntranuke2018` (the AR23 FSI model) | 5 RES events (2 CC, 3 NC), E = 1.5-3.4 GeV | The residual nucleus after FSI knock-out (8H, 11He, 14Li, 26N, 29O) is not in the PDG table. Its mass is only printed in an INFO message; the remnant stays a `HadrBlob`. The events are kept. |
| 1 | `utils::kinematics::CohQ2Lim` | one NC COH event, E = 3.38 GeV | The computed Q2min is slightly negative and is clamped to 0, as the message says. The event is kept. |

The spline files are not committed.

### Fallback: computing splines locally

`scripts/dune/xsec.sh` (GENIE 3.06.02, Ar40, AR23_20i_00_000, Default list;
defaults: flavours 12,-12,14,-14,16,-16, EMAX 120 GeV, 500 knots) is kept only as
a fallback:

```bash
source ~/genie_setup.sh
scripts/dune/xsec.sh -p 14 -e 120 -n 500 -o <dir>/ar23_ar40_14.xml   # one flavour
scripts/dune/xsec.sh merge -o <dir>/ar23_ar40.xml <dir>/ar23_ar40_12.xml,<dir>/ar23_ar40_-12.xml,...
```

It is not usable as it stands for AR23 on argon. The `NievesQELCCPXSec` (CCQE)
knot just above threshold (E = 0.133 GeV) did not finish within 90 minutes for
AR23 x Ar40, while the same knot took 6 s for AR23 x O16 and 0.1 s for
G18_10b x Ar40 (work-area report `reports/07_knot_timing.md`). Away from
threshold, one Ar40 CCQE spline with 500 knots up to 120 GeV would take about
21 CPU hours; the near-threshold knots add at least 20 hours, with no known
upper bound.
