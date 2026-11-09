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
