// =============================================================================
//  MakeOscCov.C - DUNE oscillation parameter file (oscCovDUNE.root): names,
//  prior values and diagonal covariance, in the layout the GUNDAM config reads
//  (osc_param_names TObjArray of TObjString, osc_param_priors TVectorD,
//  osc_param_cov TMatrixD), the DUNE counterpart of MakeOscCov in
//  src/MakeCovariances.C.
//
//    root -l -b -q 'src/dune/MakeOscCov.C("<output.root>")'   (returns 1 on success;
//    the output must not exist)
//
//  Source of every number: arXiv:2006.16043v2 (DUNE TDR long-baseline paper),
//  Table 8, p. 21 (NuFIT 4.0 central values; relative uncertainty = 1/6 of the
//  3 sigma range), normal ordering:
//    theta12 = 0.5903 rad, 2.3 %;  theta23 (NO) = 0.866 rad, 4.1 %;
//    theta13 (NO) = 0.150 rad, 1.5 %;  Dm2_21 = 7.39e-5 eV^2, 2.8 %;
//    Dm2_32 (NO) = 2.451e-3 eV^2, 1.3 %;  rho = 2.848 g/cm^3, 2 %.
//  The GUNDAM parameters are sin^2 theta, so prior = sin^2(theta) and
//  sigma(sin^2 theta) = sin(2 theta) x (relative uncertainty x theta).
//  delta_CP: Tables 6 and 7 (p. 21) use delta_CP = 0 for NO; Table 8 gives no
//    uncertainty. The value pi in the matrix is a placeholder: delta_CP has a
//    Flat prior in the GUNDAM config and is not constrained by it.
//  PMNS_SIGN_MASS_SQUARED_32 = +1 (normal ordering), sigma 0.1 as in the HK file;
//    the parameter is fixed in the GUNDAM config.
//  The matter density (2 %) is not a GUNDAM parameter here; the dials use
//    2.848 g/cm^3 fixed (DUNE_GLoBES.glb:11, Table 8).
// =============================================================================
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "TFile.h"
#include "TMatrixD.h"
#include "TObjArray.h"
#include "TObjString.h"
#include "TSystem.h"
#include "TVectorD.h"

int MakeOscCov(std::string output_file)
{
  if (!gSystem->AccessPathName(output_file.c_str())) { printf("[ERROR] output exists: %s\n", output_file.c_str()); return 0; }
  const double th12 = 0.5903, th23 = 0.866, th13 = 0.150;          // Table 8, rad
  const double r12 = 0.023, r23 = 0.041, r13 = 0.015;              // Table 8, relative
  auto s2 = [](double t) { return std::pow(std::sin(t), 2); };
  auto ds2 = [](double t, double r) { return std::sin(2. * t) * r * t; };
  struct Par { const char *name; double prior; double sigma; const char *note; };
  const std::vector<Par> par = {
      {"PMNS_SIN_SQUARED_12", s2(th12), ds2(th12, r12), "Table 8: theta12 0.5903 rad, 2.3 %"},
      {"PMNS_SIN_SQUARED_13", s2(th13), ds2(th13, r13), "Table 8: theta13 (NO) 0.150 rad, 1.5 %"},
      {"PMNS_SIN_SQUARED_23", s2(th23), ds2(th23, r23), "Table 8: theta23 (NO) 0.866 rad, 4.1 %"},
      {"PMNS_DELTA_MASS_SQUARED_21", 7.39e-5, 7.39e-5 * 0.028, "Table 8: 7.39e-5 eV^2, 2.8 %"},
      {"PMNS_DELTA_MASS_SQUARED_32", 2.451e-3, 2.451e-3 * 0.013, "Table 8: Dm2_32 (NO) 2.451e-3 eV^2, 1.3 %"},
      {"PMNS_DELTA_CP", 0.0, M_PI, "Tables 6/7: 0 (NO); no uncertainty given, pi is a placeholder (Flat prior)"},
      {"PMNS_SIGN_MASS_SQUARED_32", 1.0, 0.1, "normal ordering; fixed in the config"}};
  const int n = (int)par.size();
  TObjArray names(n); names.SetName("osc_param_names"); names.SetOwner(true);
  TVectorD priors(n); TMatrixD cov(n, n);
  for (int i = 0; i < n; ++i) { names.Add(new TObjString(par[i].name)); priors(i) = par[i].prior; cov(i, i) = par[i].sigma * par[i].sigma; }
  TFile *f = TFile::Open(output_file.c_str(), "CREATE");
  if (!f || !f->IsWritable()) { printf("[ERROR] cannot create %s\n", output_file.c_str()); return 0; }
  names.Write("osc_param_names", TObject::kSingleKey);
  priors.Write("osc_param_priors");
  cov.Write("osc_param_cov");
  f->Close();
  printf("\n %-30s %14s %14s %9s   %s\n", "parameter", "prior", "sigma", "relative", "source");
  for (int i = 0; i < n; ++i)
    printf(" %-30s %14.9g %14.9g %8.3f%%   %s\n", par[i].name, par[i].prior, par[i].sigma,
           par[i].prior != 0. ? 100. * par[i].sigma / std::fabs(par[i].prior) : 0., par[i].note);
  printf("\n[INFO] wrote %s\n", output_file.c_str());
  return 1;
}
