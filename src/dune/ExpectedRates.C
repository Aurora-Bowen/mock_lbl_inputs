// Expected interactions per DUNE far-detector sample, before oscillation, efficiency and
// smearing, and the number of events to generate. Run by scripts/dune/rates.sh.
//
// Inputs
//   xsec_dir/xsec_<flavour>.root  gspl2root output (GENIE AR23_20i_00_000, Ar40), one
//                                 directory per file with 1000-point graphs tot_cc, tot_nc
//                                 in 1e-38 cm^2 per Ar40 nucleus
//   flux_dir/flux_dune_{neutrino,antineutrino}_FD_uniform.root
//                                 0.125 GeV bins, content = integrated flux per bin, nu/m^2/POT
//   response                      tdr2017_response.root, for the GENIE 2.12 cross-check only
//
// For each sample, with E_i the bin centres and Phi_i the contents of the parent-flavour
// histogram:
//   N_cc = POT * N_Ar * sum_i Phi_i * 1e-4 * tot_cc(E_i) * 1e-38   (probe flavour)
//   N_nc = same with tot_nc for Default samples, 0 for CC samples;  N = N_cc + N_nc
// CC-list samples count every process of the gevgen CC list. tot_cc of the tune's Default
// list holds all of them except QEL-CC-LAMBDA when the Default list lacks it (G00_00b), so
//   N_cc(CC list) = N_cc(tot_cc) + N(QEL-CC-LAMBDA graphs qel_cc_*_strange* of
//                   xsec_dir/xsec_<flavour>_CC.root, gspl2root --event-generator-list CC),
// the second term only if the Default file has no qel_cc_*_strange* graph (AR23 has them:
// nothing is added). Check: tot_cc(CC list) + imd_cc + imdanh_cc (electron-target CC, in
// tot_cc of Default, not in the CC list) = tot_cc(Default) + added, within 1e-6 per sample.
//   nevents = max(100000, 20 N rounded up to the next multiple of 10000)
// tot_cc/tot_nc are evaluated with TGraph::Eval (linear interpolation).
//
// Exposure (lbl_config/dune/tdr2017/globes_config/): NUTIME = NUBARTIME = 6.5 yr,
// LAMASS = 40 kt (definitions.inc:3-5); @power = 11.0 [1e20 POT/yr at 1.2 MW]
// (flux/Beam.inc:20,27). POT = 6.5 * 11.0e20 = 7.15e21 per beam mode.
//
// Checks (exit status 1 and no output file if any fails):
//   - every nevents <= 2,000,000;
//   - for the 8 intrinsic samples, N_cc(AR23) / N_cc(GENIE 2.12 GLoBES table) in [0.7, 1.3].
// The GENIE 2.12 tables (xsec/CC/<flavour>, x = E [GeV], y = sigma/E in 1e-38 cm^2/GeV per
// nucleon) are multiplied by E and by 40; outside the table's energy range the sigma/E of the
// nearest point is used.
// Also printed, for information: the contributions of the electron-target graphs imd_cc,
// imdanh_cc (CC, inside tot_cc but not in the gevgen CC list), ve_nc (inside tot_nc) and
// ve_ccncmix (nu-e elastic of nue/nuebar, in neither tot_cc nor tot_nc).
//
// Usage: ExpectedRates(xsec_dir, flux_dir, response, out_tsv, spline, spline_sha256, tune,
//                      git_commit, code_sha256, cc_list_desc)  -> 0 ok, 1 check failed, 2 I/O error.
//        cc_list_desc: the processes of the CC list (from rates.sh), recorded in the header.

#include "TFile.h"
#include "TH1D.h"
#include "TGraph.h"
#include "TKey.h"
#include "TDirectory.h"
#include "TSystem.h"
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

struct Sample {
  std::string name, beam, flux_hist, list;
  int pdg;
};

const char *FlavourOf(int pdg)
{
  switch (pdg) {
  case 12: return "nue";     case -12: return "nuebar";
  case 14: return "numu";    case -14: return "numubar";
  case 16: return "nutau";   case -16: return "nutaubar";
  }
  return "?";
}

TDirectory *OnlyDir(TFile &f)
{
  TDirectory *d = nullptr; int n = 0;
  for (TObject *o : *f.GetListOfKeys()) {
    TKey *k = (TKey *)o;
    if (std::string(k->GetClassName()) == "TDirectoryFile") { d = (TDirectory *)f.Get(k->GetName()); ++n; }
  }
  return n == 1 ? d : nullptr;
}

// sum_i Phi_i * sigma(E_i), sigma from graph g scaled by `scale` (graph may be null -> 0)
double Fold(const TH1D *h, const TGraph *g, double scale = 1.)
{
  if (!g) return 0.;
  double s = 0.;
  for (int i = 1; i <= h->GetNbinsX(); ++i) {
    const double phi = h->GetBinContent(i);
    if (phi != 0.) s += phi * g->Eval(h->GetBinCenter(i)) * scale;
  }
  return s;
}

// GENIE 2.12 GLoBES table: sigma/E per nucleon, nearest point outside the range
double Fold212(const TH1D *h, const TGraph *g)
{
  const int n = g->GetN();
  const double *x = g->GetX(), *y = g->GetY();
  double s = 0.;
  for (int i = 1; i <= h->GetNbinsX(); ++i) {
    const double phi = h->GetBinContent(i), e = h->GetBinCenter(i);
    if (phi == 0.) continue;
    double soe;
    if (e <= x[0]) soe = y[0];
    else if (e >= x[n - 1]) soe = y[n - 1];
    else soe = g->Eval(e);
    s += phi * soe * e * 40.;
  }
  return s;
}

// sum_i Phi_i * sum of the QEL-CC-LAMBDA graphs (qel_cc_<p|n>_strange<pdg>) of directory d;
// nstrange = number of such graphs
double FoldStrange(const TH1D *h, TDirectory *d, int &nstrange)
{
  nstrange = 0; double s = 0.;
  for (TObject *o : *d->GetListOfKeys()) {
    std::string n = o->GetName();
    if (n.rfind("qel_cc_", 0) != 0 || n.find("_strange") == std::string::npos) continue;
    ++nstrange; s += Fold(h, dynamic_cast<TGraph *>(d->Get(n.c_str())));
  }
  return s;
}

} // namespace

int ExpectedRates(const char *xsec_dir, const char *flux_dir, const char *response,
                  const char *out_tsv, const char *spline, const char *spline_sha,
                  const char *tune, const char *git_commit, const char *code_sha,
                  const char *cc_list_desc = "")
{
  // exposure
  const double kYears = 6.5, kPowerE20 = 11.0, kMassKt = 40., kMW = 1.2;
  const double POT = kYears * kPowerE20 * 1e20;
  const double kNA = 6.02214076e23, kMolarAr40 = 39.9623831;
  const double NAr = kMassKt * 1e9 * kNA / kMolarAr40;
  const double kt_MW_yr = kMassKt * kMW * 2. * kYears;
  printf("POT per beam mode = %.6g\nN_Ar40 = %.6e\nexposure = %g kt-MW-yr (FHC + RHC)\n", POT, NAr, kt_MW_yr);
  const double kNorm = POT * NAr * 1e-4 * 1e-38; // Phi [nu/m^2/POT] -> [nu/cm^2/POT]; sigma [1e-38 cm^2]

  if (!gSystem->AccessPathName(out_tsv)) { printf("ERROR: %s exists\n", out_tsv); return 2; }

  // cross sections
  const int pdgs[] = {12, -12, 14, -14, 16, -16};
  std::map<int, std::map<std::string, TGraph *>> xs;
  std::vector<TFile *> keep;
  for (int p : pdgs) {
    std::string path = Form("%s/xsec_%s.root", xsec_dir, FlavourOf(p));
    TFile *f = TFile::Open(path.c_str(), "READ");
    if (!f || f->IsZombie()) { printf("ERROR: cannot open %s\n", path.c_str()); return 2; }
    keep.push_back(f);
    TDirectory *d = OnlyDir(*f);
    if (!d) { printf("ERROR: %s: expected exactly one directory\n", path.c_str()); return 2; }
    for (const char *g : {"tot_cc", "tot_nc", "imd_cc", "imdanh_cc", "ve_nc", "ve_ccncmix"})
      xs[p][g] = dynamic_cast<TGraph *>(d->Get(g));
    TGraph *cc = xs[p]["tot_cc"], *nc = xs[p]["tot_nc"];
    if (!cc || !nc) { printf("ERROR: %s: tot_cc or tot_nc missing\n", path.c_str()); return 2; }
    printf("%-9s %s/%s: tot_cc %d points %.4g-%.4g GeV; sigma_cc(1,10 GeV) = %.4g, %.4g; "
           "sigma_nc(1,10 GeV) = %.4g, %.4g [1e-38 cm^2/nucleus]; extra graphs:%s%s%s%s\n",
           FlavourOf(p), path.c_str(), d->GetName(), cc->GetN(), cc->GetX()[0], cc->GetX()[cc->GetN() - 1],
           cc->Eval(1.), cc->Eval(10.), nc->Eval(1.), nc->Eval(10.),
           xs[p]["imd_cc"] ? " imd_cc" : "", xs[p]["imdanh_cc"] ? " imdanh_cc" : "",
           xs[p]["ve_nc"] ? " ve_nc" : "", xs[p]["ve_ccncmix"] ? " ve_ccncmix" : "");
  }

  // other generator lists: xsec_<flavour>_CC.root for the CC-list probes
  std::map<int, TDirectory *> xsdef, xscc;
  for (size_t i = 0; i < 6; ++i) {
    const int p = pdgs[i];
    xsdef[p] = OnlyDir(*keep[i]);
    std::string path = Form("%s/xsec_%s_CC.root", xsec_dir, FlavourOf(p));
    if (gSystem->AccessPathName(path.c_str())) continue;
    TFile *f = TFile::Open(path.c_str(), "READ");
    if (!f || f->IsZombie()) { printf("ERROR: cannot open %s\n", path.c_str()); return 2; }
    keep.push_back(f);
    xscc[p] = OnlyDir(*f);
    if (!xscc[p] || !xscc[p]->Get("tot_cc")) { printf("ERROR: %s: no single directory with tot_cc\n", path.c_str()); return 2; }
  }

  // flux
  std::map<std::string, TFile *> fflux;
  fflux["fhc"] = TFile::Open(Form("%s/flux_dune_neutrino_FD_uniform.root", flux_dir), "READ");
  fflux["rhc"] = TFile::Open(Form("%s/flux_dune_antineutrino_FD_uniform.root", flux_dir), "READ");
  std::map<std::string, std::string> fname = {{"fhc", "flux_dune_neutrino_FD_uniform.root"},
                                              {"rhc", "flux_dune_antineutrino_FD_uniform.root"}};
  for (auto &kv : fflux)
    if (!kv.second || kv.second->IsZombie()) { printf("ERROR: cannot open %s flux\n", kv.first.c_str()); return 2; }

  TFile fresp(response, "READ");
  if (fresp.IsZombie()) { printf("ERROR: cannot open %s\n", response); return 2; }

  // samples
  std::vector<Sample> samples;
  for (std::string beam : {"fhc", "rhc"}) {
    samples.push_back({"numu_x_numu_" + beam, beam, "numu_flux", "Default", 14});
    samples.push_back({"numubar_x_numubar_" + beam, beam, "numubar_flux", "Default", -14});
    samples.push_back({"nue_x_nue_" + beam, beam, "nue_flux", "Default", 12});
    samples.push_back({"nuebar_x_nuebar_" + beam, beam, "nuebar_flux", "Default", -12});
    samples.push_back({"numu_x_nue_" + beam, beam, "numu_flux", "CC", 12});
    samples.push_back({"numubar_x_nuebar_" + beam, beam, "numubar_flux", "CC", -12});
    samples.push_back({"numu_x_nutau_" + beam, beam, "numu_flux", "CC", 16});
    samples.push_back({"numubar_x_nutaubar_" + beam, beam, "numubar_flux", "CC", -16});
  }

  struct Row { Sample s; double ncc, nnc, n; long nev; };
  std::set<int> lam_added, lam_in_default;
  std::vector<Row> rows;
  int nfail = 0;
  long nev_sum = 0;
  printf("\n%-24s %-4s %-13s %4s %-7s %12s %12s %12s %9s | %10s %10s %10s %10s\n", "sample", "beam", "flux_hist",
         "pdg", "list", "N_cc", "N_nc", "N", "nevents", "imd_cc", "imdanh_cc", "ve_nc", "ve_ccncmix");
  for (const Sample &s : samples) {
    TH1D *h = dynamic_cast<TH1D *>(fflux[s.beam]->Get(s.flux_hist.c_str()));
    if (!h || h->GetXaxis()->IsVariableBinSize() || h->GetNbinsX() != 944) {
      printf("ERROR: %s: flux histogram %s missing or not 944 uniform bins\n", s.name.c_str(), s.flux_hist.c_str());
      return 2;
    }
    auto &g = xs[s.pdg];
    double ncc = kNorm * Fold(h, g["tot_cc"]), nlam = 0.;
    if (s.list == "CC") {
      if (!xscc.count(s.pdg)) { printf("ERROR: %s: xsec_%s_CC.root missing\n", s.name.c_str(), FlavourOf(s.pdg)); return 2; }
      int ndef = 0, ncl = 0;
      const double lam_def = kNorm * FoldStrange(h, xsdef[s.pdg], ndef);
      const double lam_cc = kNorm * FoldStrange(h, xscc[s.pdg], ncl);
      if (ndef == 0) { nlam = lam_cc; if (ncl) lam_added.insert(s.pdg); }
      else lam_in_default.insert(s.pdg);
      const double ncclist = kNorm * Fold(h, dynamic_cast<TGraph *>(xscc[s.pdg]->Get("tot_cc")));
      const double nimd = kNorm * (Fold(h, g["imd_cc"]) + Fold(h, g["imdanh_cc"]));
      const double rel = (ncclist + nimd - (ncc + nlam)) / ncc;
      printf("CC-LIST %-24s tot_cc(Default) %.6e  tot_cc(CC list) %.6e  imd+imdanh %.4e  Lambda graphs: Default %d (%.4e), CC list %d (%.4e)  added %.6e (%.4f %%)  "
             "check (CClist + imd - Default - added)/Default = %.2e -> %s\n",
             s.name.c_str(), ncc, ncclist, nimd, ndef, lam_def, ncl, lam_cc, nlam, 100. * nlam / ncc, rel,
             std::fabs(rel) < 1e-6 ? "PASS" : "FAIL");
      if (std::fabs(rel) >= 1e-6) { printf("CHECK FAIL: %s: CC-list sum not reproduced\n", s.name.c_str()); ++nfail; }
      ncc += nlam;
    }
    const double nnc = (s.list == "Default") ? kNorm * Fold(h, g["tot_nc"]) : 0.;
    const double n = ncc + nnc;
    long nev = (long)std::ceil(20. * n / 10000.) * 10000;
    if (nev < 100000) nev = 100000;
    nev_sum += nev;
    // electron-target pieces, as counts, for information
    const double nimd = kNorm * Fold(h, g["imd_cc"]), nimda = kNorm * Fold(h, g["imdanh_cc"]);
    const double nve = kNorm * Fold(h, g["ve_nc"]), nvemix = kNorm * Fold(h, g["ve_ccncmix"]);
    printf("%-24s %-4s %-13s %4d %-7s %12.5e %12.5e %12.5e %9ld | %10.3e %10.3e %10.3e %10.3e\n", s.name.c_str(),
           s.beam.c_str(), s.flux_hist.c_str(), s.pdg, s.list.c_str(), ncc, nnc, n, nev, nimd, nimda, nve, nvemix);
    if (nev > 2000000) { printf("CHECK FAIL: %s nevents %ld > 2000000\n", s.name.c_str(), nev); ++nfail; }
    rows.push_back({s, ncc, nnc, n, nev});
  }
  printf("sum of nevents = %ld\n", nev_sum);

  // relative size of the electron-target pieces
  printf("\nElectron-target contributions relative to the sample (N_cc for imd*, N for ve*):\n");
  for (const Row &r : rows) {
    TH1D *h = (TH1D *)fflux[r.s.beam]->Get(r.s.flux_hist.c_str());
    auto &g = xs[r.s.pdg];
    const double imd = kNorm * (Fold(h, g["imd_cc"]) + Fold(h, g["imdanh_cc"]));
    const double vemix = kNorm * Fold(h, g["ve_ccncmix"]);
    const double ve = kNorm * Fold(h, g["ve_nc"]);
    printf("  %-24s (imd_cc+imdanh_cc)/N_cc = %.3e   ve_nc/N = %.3e   ve_ccncmix/N = %.3e\n", r.s.name.c_str(),
           r.ncc > 0 ? imd / r.ncc : 0., r.n > 0 ? ve / r.n : 0., r.n > 0 ? vemix / r.n : 0.);
  }

  // GENIE 2.12 cross-check, intrinsic samples
  printf("\nCross-check with the GENIE 2.12 GLoBES tables (%s, xsec/CC and xsec/NC):\n", response);
  for (const Row &r : rows) {
    if (r.s.list != "Default") continue;
    const char *fl = FlavourOf(r.s.pdg);
    TGraph *gcc = dynamic_cast<TGraph *>(fresp.Get(Form("xsec/CC/%s", fl)));
    TGraph *gnc = dynamic_cast<TGraph *>(fresp.Get(Form("xsec/NC/%s", fl)));
    if (!gcc || !gnc) { printf("ERROR: xsec/CC|NC/%s missing in %s\n", fl, response); return 2; }
    TH1D *h = (TH1D *)fflux[r.s.beam]->Get(r.s.flux_hist.c_str());
    const double cc212 = kNorm * Fold212(h, gcc), nc212 = kNorm * Fold212(h, gnc);
    const double ratio = r.ncc / cc212;
    const bool ok = ratio >= 0.7 && ratio <= 1.3;
    printf("  %-24s table E range %.4g-%.4g GeV; N_cc(2.12) = %.5e  AR23/2.12 = %.4f -> %s;  "
           "N_nc(2.12) = %.5e  AR23/2.12 (info) = %.4f\n",
           r.s.name.c_str(), gcc->GetX()[0], gcc->GetX()[gcc->GetN() - 1], cc212, ratio, ok ? "PASS" : "FAIL", nc212,
           nc212 > 0 ? r.nnc / nc212 : 0.);
    if (!ok) ++nfail;
  }

  if (nfail) { printf("\n%d check(s) failed; %s not written\n", nfail, out_tsv); return 1; }

  FILE *out = fopen(out_tsv, "wx");
  if (!out) { printf("ERROR: cannot create %s\n", out_tsv); return 2; }
  fprintf(out, "# DUNE FD expected interactions per sample (before oscillation, efficiency, smearing) and events to generate.\n");
  fprintf(out, "# Written by scripts/dune/rates.sh (src/dune/ExpectedRates.C); git HEAD at run time %s; %s\n", git_commit, code_sha);
  fprintf(out, "# POT = %.6g per beam mode (NUTIME = NUBARTIME = 6.5 yr, definitions.inc:3-4; @power = 11.0e20 POT/yr at 1.2 MW, flux/Beam.inc:20,27)\n", POT);
  fprintf(out, "# N_Ar40 = %.6e (LAMASS = 40 kt, definitions.inc:5; 40e9 g * 6.02214076e23 / 39.9623831 g/mol)\n", NAr);
  fprintf(out, "# exposure = %g kt-MW-yr (40 kt x 1.2 MW x (6.5 + 6.5) yr)\n", kt_MW_yr);
  fprintf(out, "# splines: %s sha256 %s; tune %s; gspl2root tot_cc / tot_nc for Ar40\n", spline, spline_sha, tune);
  fprintf(out, "# flux: lbl_config/dune/tdr2017/flux/<flux_file> (0.125 GeV bins), parent-flavour histogram <flux_hist>\n");
  fprintf(out, "# N_cc = POT * N_Ar * sum_i Phi_i * 1e-4 * tot_cc(E_i) * 1e-38; N_nc same with tot_nc (Default list only, else 0); N = N_cc + N_nc\n");
  std::string added, indef;
  for (int p : lam_added) added += Form(" %d", p);
  for (int p : lam_in_default) indef += Form(" %d", p);
  fprintf(out, "# CC-list samples (%s): N_cc = tot_cc of the Default list + QEL-CC-LAMBDA (qel_cc_*_strange* of gspl2root --event-generator-list CC) where the Default list has none; "
               "added for probe(s):%s; already in tot_cc for probe(s):%s. tot_cc also holds IMD/IMD-ANH (not in the CC list)\n",
          cc_list_desc, added.empty() ? " none" : added.c_str(), indef.empty() ? " none" : indef.c_str());
  fprintf(out, "# nevents = max(100000, 20 * N rounded up to the next multiple of 10000)\n");
  fprintf(out, "sample\tbeam\tflux_file\tflux_hist\tprobe_pdg\tlist\tN_cc\tN_nc\tN\tnevents\n");
  for (const Row &r : rows)
    fprintf(out, "%s\t%s\t%s\t%s\t%d\t%s\t%.6e\t%.6e\t%.6e\t%ld\n", r.s.name.c_str(), r.s.beam.c_str(),
            fname[r.s.beam].c_str(), r.s.flux_hist.c_str(), r.s.pdg, r.s.list.c_str(), r.ncc, r.nnc, r.n, r.nev);
  fclose(out);
  printf("\nwrote %s (%zu samples, sum of nevents %ld)\n", out_tsv, rows.size(), nev_sum);
  return 0;
}
