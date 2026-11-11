// Uniform-bin copies of the DUNE TDR far-detector flux histograms.
//
// The *_flux histograms in lbl_config/dune/tdr2017/flux/flux_dune_*_FD.root hold the
// per-bin integrated flux (nu/m^2/POT) on variable-width bins (0.125 GeV up to 8 GeV,
// 0.5 GeV up to 20 GeV, 2 GeV up to 118 GeV). gevgen reads them correctly with NOWIDTH,
// but NUISANCE (nuisflat) normalises with FluxHist.Integral("width"), which treats the
// content as a density. These copies put the same content on 944 bins of 0.125 GeV over
// 0-118 GeV: each original bin's content is divided equally among the 1, 4 or 16 new bins
// that cover it, so the content stays "integrated flux per bin, nu/m^2/POT" and every
// bin has the same width.
//
// Usage (from the repository root, see scripts/dune/uniform_flux.sh):
//   root -l -b -q 'src/dune/MakeUniformFlux.C+("in.root","out.root")'
// Returns 0 if every check passes, 1 if a check fails, 2 on an I/O error.
// The output file must not exist (opened with "CREATE").
//
// Checks, printed per histogram:
//   - every original bin edge lies on the 0.125 GeV grid, first edge 0, last edge 118;
//   - underflow and overflow of the original are 0;
//   - for every original bin, the sum of its new bins equals its content
//     (relative difference < 1e-12; exact 0 required if the content is 0);
//   - the total is unchanged (relative difference < 1e-12);
//   - GetXaxis()->IsVariableBinSize() is false for the output.

#include "TFile.h"
#include "TH1D.h"
#include "TAxis.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

int MakeUniformFlux(const char *in_path, const char *out_path)
{
  const int kNbins = 944;
  const double kXmin = 0., kXmax = 118., kStep = 0.125;
  const double kTol = 1e-12;
  const char *names[] = {"numu_flux", "numubar_flux", "nue_flux", "nuebar_flux"};

  TFile fin(in_path, "READ");
  if (fin.IsZombie()) { printf("ERROR: cannot open %s\n", in_path); return 2; }
  TFile fout(out_path, "CREATE");
  if (fout.IsZombie() || !fout.IsWritable()) {
    printf("ERROR: cannot create %s (does it exist already?)\n", out_path); return 2;
  }

  printf("input : %s\noutput: %s\n", in_path, out_path);
  int nfail = 0;
  for (const char *name : names) {
    TH1D *h = dynamic_cast<TH1D *>(fin.Get(name));
    if (!h) { printf("ERROR: %s: no TH1D %s\n", in_path, name); return 2; }
    const TAxis *ax = h->GetXaxis();
    const int n = h->GetNbinsX();
    printf("== %s: %d original bins, %g-%g GeV, variable bins: %s\n", name, n,
           ax->GetXmin(), ax->GetXmax(), ax->IsVariableBinSize() ? "yes" : "no");

    // edges on the grid
    double max_off = 0.;
    bool edges_ok = std::fabs(ax->GetBinLowEdge(1) - kXmin) < 1e-9 &&
                    std::fabs(ax->GetBinUpEdge(n) - kXmax) < 1e-9;
    std::vector<int> lo(n + 2), hi(n + 2); // new-bin index range [lo, hi] of original bin i
    for (int i = 1; i <= n; ++i) {
      double a = ax->GetBinLowEdge(i) / kStep, b = ax->GetBinUpEdge(i) / kStep;
      double off = std::max(std::fabs(a - std::round(a)), std::fabs(b - std::round(b)));
      if (off > max_off) max_off = off;
      lo[i] = (int)std::lround(a) + 1;
      hi[i] = (int)std::lround(b);
      if (off > 1e-9 || hi[i] < lo[i]) edges_ok = false;
    }
    printf("   CHECK edges on 0.125 GeV grid (0..118): max |edge/0.125 - round| = %.3g -> %s\n",
           max_off, edges_ok ? "PASS" : "FAIL");
    if (!edges_ok) { ++nfail; continue; }

    const double uf = h->GetBinContent(0), of = h->GetBinContent(n + 1);
    const bool uo_ok = (uf == 0. && of == 0.);
    printf("   CHECK underflow %g, overflow %g -> %s\n", uf, of, uo_ok ? "PASS" : "FAIL");
    if (!uo_ok) ++nfail;

    fout.cd();
    TH1D *u = new TH1D(name, h->GetTitle(), kNbins, kXmin, kXmax);
    u->SetDirectory(&fout);
    u->GetXaxis()->SetTitle(ax->GetTitle());
    u->GetYaxis()->SetTitle(h->GetYaxis()->GetTitle());
    std::vector<int> width_count(17, 0);
    for (int i = 1; i <= n; ++i) {
      const int m = hi[i] - lo[i] + 1;
      if (m < (int)width_count.size()) width_count[m]++;
      const double c = h->GetBinContent(i) / m;
      for (int j = lo[i]; j <= hi[i]; ++j) {
        if (u->GetBinContent(j) != 0.) { printf("ERROR: new bin %d filled twice\n", j); return 2; }
        u->SetBinContent(j, c);
      }
    }
    u->SetEntries(h->GetEntries());
    printf("   original bins split into 1/4/16 new bins: %d/%d/%d\n",
           width_count[1], width_count[4], width_count[16]);

    // per-bin sums
    double max_rel = 0.; int nbad = 0;
    for (int i = 1; i <= n; ++i) {
      double s = 0.;
      for (int j = lo[i]; j <= hi[i]; ++j) s += u->GetBinContent(j);
      const double c = h->GetBinContent(i);
      const double rel = (c != 0.) ? std::fabs(s - c) / std::fabs(c) : (s == 0. ? 0. : 1.);
      if (rel > max_rel) max_rel = rel;
      if (!(rel < kTol)) ++nbad;
    }
    printf("   CHECK per-bin sum of new bins = original content: max rel diff %.3g, bins failing %d -> %s\n",
           max_rel, nbad, nbad == 0 ? "PASS" : "FAIL");
    if (nbad) ++nfail;

    // total
    double t0 = 0., t1 = 0.;
    for (int i = 1; i <= n; ++i) t0 += h->GetBinContent(i);
    for (int j = 1; j <= kNbins; ++j) t1 += u->GetBinContent(j);
    const double trel = std::fabs(t1 - t0) / std::fabs(t0);
    printf("   CHECK total: original %.15e, uniform %.15e, rel diff %.3g -> %s\n", t0, t1, trel,
           trel < kTol ? "PASS" : "FAIL");
    if (!(trel < kTol)) ++nfail;

    const bool var = u->GetXaxis()->IsVariableBinSize();
    printf("   CHECK output IsVariableBinSize() = %s, %d bins, width %g GeV -> %s\n",
           var ? "true" : "false", u->GetNbinsX(), u->GetXaxis()->GetBinWidth(1), var ? "FAIL" : "PASS");
    if (var) ++nfail;

    u->Write();
  }
  fout.Close();

  // re-read the written file and repeat the binning check on what is on disk
  TFile fchk(out_path, "READ");
  for (const char *name : names) {
    TH1D *u = dynamic_cast<TH1D *>(fchk.Get(name));
    const bool ok = u && !u->GetXaxis()->IsVariableBinSize() && u->GetNbinsX() == kNbins;
    printf("   CHECK re-read %s: %s -> %s\n", name, u ? "found" : "missing", ok ? "PASS" : "FAIL");
    if (!ok) ++nfail;
  }
  printf("%s: %d failed checks\n", out_path, nfail);
  return nfail ? 1 : 0;
}
