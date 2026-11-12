// =============================================================================
//  FoldResponse.C - DUNE far-detector fake reconstruction: fold one GENIE flat
//  tree with the TDR GLoBES response (tdr2017_response.root), event by event.
//
//  Usage (from the repository root; scripts/dune/recon.sh builds the call):
//    root -l -b -q 'src/dune/FoldResponse.C("<flattree>","<fakerecon>","<sample>",
//        "<FHC|RHC>",<flux_pdg>,<N>,<nevents>,<seed>,"<response.root>",
//        "<channels.tsv>","<rules.tsv>","<provenance>")'
//  Prints FOLD_OK on success (root -q exits with the macro's return value, so
//  the caller must not rely on the exit status).
//
//  What GLoBES computes per channel c (reconstructed bin i, true sampling bin k):
//      rate_c(i) = eff_c[i] x sum_k R_c[i,k] x (flux x xsec)(k)
//  Here every MC event stands for N / nevents interactions at 624 kt-MW-yr
//  before oscillation (gen/dune/rates.tsv), and the sum over k is done by
//  sampling: for each channel the event belongs to, a reconstructed bin i is
//  drawn from column k of that channel's smearing matrix (probabilities
//  R[i,k] / sum_i R[i,k]) and the event gets the post-smearing efficiency
//  eff_c[i] as its selection weight.
//
//  Channel lookup (all from channels.tsv, nothing hard-coded): an event matches
//  channel c when
//    beam_mode(c)      = the sample's beam mode (FHC/RHC),
//    polarity(c)       = sign of the flux flavour ('+' neutrino, '-' antineutrino),
//    initial_flavour(c)= flux flavour (e/m), and either
//    CC event: xsec_name(c) = CC and final_flavour(c) = event flavour (e/m/t), or
//    NC event: xsec_name(c) = NC and final_flavour(c) = NOSC_<flux flavour>.
//  An event matches 0, 1 or 2 channels; one output row per (event, channel).
//
//  Output tree FlatTree_VARS: every branch of the input flat tree (as the HK
//  FakeRecon.C), plus
//    Enu_rec      F  reconstructed energy, uniform inside the drawn bin [GeV]
//    POTWeight    D  N / nevents (interactions per event, before oscillation)
//    SelEffWeight D  eff_c[reco_bin]
//    SampleIndex  I  GLoBES rule of the channel: 0 nue_app, 1 numu_dis,
//                    2 nuebar_app, 3 numubar_dis
//    ChannelIndex I  0-based data row of channels.tsv
//    flux_pdg     I  flux (parent) flavour of the sample
//    isRHC        O  beam mode of the sample
//    flat_entry   L  entry number in the input flat tree (= GHEP entry)
//    true_bin     I  0-based true sampling bin k (x axis of smearing/<energy_name>)
//    reco_bin     I  0-based reconstructed bin i
//  No row is written when Enu_true is outside the matrix x range or column k of
//  the matrix sums to 0; these are counted per channel. No event is dropped
//  for any other reason; OscWeight is not written (no oscillation here).
//  fScaleFactor is carried over from the flat tree but is not used: the
//  normalisation is POTWeight (lbl_config/dune/README.md, "Generation").
//  TNamed objects record the sample, channels, rules, N, nevents, seed,
//  provenance (git commit, sha256 of the macro, script and response file).
//  The output file must not exist (opened with option CREATE).
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "TFile.h"
#include "TH1D.h"
#include "TH2D.h"
#include "TNamed.h"
#include "TRandom3.h"
#include "TSystem.h"
#include "TTree.h"

namespace fold_response {

std::string Trim(const std::string &s)
{
  const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Reads a tab-separated table with a header line; returns rows as maps.
bool ReadTsv(const std::string &file, std::vector<std::map<std::string, std::string>> &rows)
{
  std::ifstream in(file.c_str());
  if (!in) return false;
  std::string line;
  std::vector<std::string> head;
  while (std::getline(in, line)) {
    if (Trim(line).empty()) continue;
    std::vector<std::string> f;
    std::stringstream ss(line);
    std::string t;
    while (std::getline(ss, t, '\t')) f.push_back(Trim(t));
    if (head.empty()) { head = f; continue; }
    std::map<std::string, std::string> r;
    for (size_t i = 0; i < head.size() && i < f.size(); ++i) r[head[i]] = f[i];
    rows.push_back(r);
  }
  return !head.empty();
}

char FlavourCode(int pdg)
{
  switch (std::abs(pdg)) {
  case 12: return 'e';
  case 14: return 'm';
  case 16: return 't';
  }
  return '?';
}

struct Channel {
  int index;                // 0-based data row of channels.tsv
  std::string name, beam, polarity, initial, final_flavour, xsec, energy, rule;
  int sample_index;         // from the rule
  TH2D *smear = nullptr;
  TH1D *eff = nullptr;
  std::vector<std::vector<double>> cum;   // [k][i] cumulative column sums
  std::vector<double> colsum;             // [k]
  long long rows = 0, n_out_of_range = 0, n_empty_column = 0, n_events = 0;
  double sum_pot = 0., sum_sel = 0.;
};

} // namespace fold_response

int FoldResponse(std::string flat_file, std::string out_file, std::string sample,
                 std::string beam, int flux_pdg, double N, long long nevents,
                 unsigned int seed, std::string response_file, std::string channels_file,
                 std::string rules_file, std::string provenance)
{
  using namespace fold_response;
  if (beam != "FHC" && beam != "RHC") { printf("[ERROR] beam must be FHC or RHC: %s\n", beam.c_str()); return 0; }
  if (std::abs(flux_pdg) != 12 && std::abs(flux_pdg) != 14) { printf("[ERROR] flux_pdg %d\n", flux_pdg); return 0; }
  if (!(N > 0.) || nevents <= 0) { printf("[ERROR] N %g, nevents %lld\n", N, nevents); return 0; }
  if (!gSystem->AccessPathName(out_file.c_str())) { printf("[ERROR] output exists: %s\n", out_file.c_str()); return 0; }

  // ---------------------------------------------------------------- tables
  std::vector<std::map<std::string, std::string>> crow, rrow;
  if (!ReadTsv(channels_file, crow) || crow.empty()) { printf("[ERROR] cannot read %s\n", channels_file.c_str()); return 0; }
  if (!ReadTsv(rules_file, rrow) || rrow.empty()) { printf("[ERROR] cannot read %s\n", rules_file.c_str()); return 0; }
  std::map<std::string, std::string> rule_of;   // channel -> rule
  for (auto &r : rrow) {
    if (rule_of.count(r["channel"]) && rule_of[r["channel"]] != r["rule"]) {
      printf("[ERROR] channel %s in two rules\n", r["channel"].c_str()); return 0; }
    rule_of[r["channel"]] = r["rule"];
  }
  const std::map<std::string, int> sample_index_of = {
      {"nue_app", 0}, {"numu_dis", 1}, {"nuebar_app", 2}, {"numubar_dis", 3}};

  const char flux_flav = FlavourCode(flux_pdg);
  const std::string polarity = flux_pdg > 0 ? "+" : "-";
  const std::string nosc = std::string("NOSC_") + flux_flav;

  TFile *fr = TFile::Open(response_file.c_str(), "READ");
  if (!fr || fr->IsZombie()) { printf("[ERROR] cannot open %s\n", response_file.c_str()); return 0; }

  // Channels this sample can feed: beam, polarity and initial flavour fixed.
  std::vector<Channel> chans;
  for (size_t j = 0; j < crow.size(); ++j) {
    auto &r = crow[j];
    if (r["beam_mode"] != beam || r["polarity"] != polarity || r["initial_flavour"] != std::string(1, flux_flav)) continue;
    Channel c;
    c.index = (int)j; c.name = r["channel"]; c.beam = r["beam_mode"]; c.polarity = r["polarity"];
    c.initial = r["initial_flavour"]; c.final_flavour = r["final_flavour"]; c.xsec = r["xsec_name"];
    c.energy = r["energy_name"];
    if (c.xsec != "CC" && c.xsec != "NC") { printf("[ERROR] channel %s xsec_name %s\n", c.name.c_str(), c.xsec.c_str()); return 0; }
    if (!rule_of.count(c.name)) { printf("[ERROR] channel %s in no rule\n", c.name.c_str()); return 0; }
    c.rule = rule_of[c.name];
    if (!sample_index_of.count(c.rule)) { printf("[ERROR] unknown rule %s\n", c.rule.c_str()); return 0; }
    c.sample_index = sample_index_of.at(c.rule);
    c.smear = (TH2D *)fr->Get(("smearing/" + c.energy).c_str());
    c.eff = (TH1D *)fr->Get(("efficiency/" + c.beam + "/" + c.name).c_str());
    if (!c.smear || !c.eff) { printf("[ERROR] response objects missing for %s\n", c.name.c_str()); return 0; }
    const int nk = c.smear->GetNbinsX(), ni = c.smear->GetNbinsY();
    if (c.eff->GetNbinsX() != ni) { printf("[ERROR] %s: efficiency bins %d != matrix rows %d\n", c.name.c_str(), c.eff->GetNbinsX(), ni); return 0; }
    for (int i = 1; i <= ni + 1; ++i)
      if (c.eff->GetXaxis()->GetBinLowEdge(i) != c.smear->GetYaxis()->GetBinLowEdge(i)) {
        printf("[ERROR] %s: efficiency and matrix reco edges differ\n", c.name.c_str()); return 0; }
    c.cum.assign(nk, std::vector<double>(ni, 0.));
    c.colsum.assign(nk, 0.);
    for (int k = 0; k < nk; ++k) {
      double s = 0.;
      for (int i = 0; i < ni; ++i) {
        const double v = c.smear->GetBinContent(k + 1, i + 1);
        if (v < 0.) { printf("[ERROR] %s: negative matrix element\n", c.name.c_str()); return 0; }
        s += v; c.cum[k][i] = s;
      }
      c.colsum[k] = s;
    }
    chans.push_back(c);
  }
  if (chans.empty()) { printf("[ERROR] no channel for beam %s, flux pdg %d\n", beam.c_str(), flux_pdg); return 0; }

  // ---------------------------------------------------------------- input
  TFile *fin = TFile::Open(flat_file.c_str(), "READ");
  if (!fin || fin->IsZombie()) { printf("[ERROR] cannot open %s\n", flat_file.c_str()); return 0; }
  TTree *flat = (TTree *)fin->Get("FlatTree_VARS");
  if (!flat) { printf("[ERROR] no FlatTree_VARS in %s\n", flat_file.c_str()); return 0; }
  const Long64_t nflat = flat->GetEntries();
  if (nflat != nevents) { printf("[ERROR] flat tree has %lld entries, nevents %lld\n", nflat, nevents); return 0; }
  float Enu_true; int PDGnu; Char_t cc;
  flat->SetBranchAddress("Enu_true", &Enu_true);
  flat->SetBranchAddress("PDGnu", &PDGnu);
  flat->SetBranchAddress("cc", &cc);

  printf("[INFO] sample %s, beam %s, flux pdg %d, N %.6e, nevents %lld, POTWeight %.6e, seed %u\n",
         sample.c_str(), beam.c_str(), flux_pdg, N, nevents, N / nevents, seed);
  printf("[INFO] channels this sample can feed (ChannelIndex name xsec final rule SampleIndex energy):\n");
  for (auto &c : chans)
    printf("         %2d %-22s %s %-7s %-12s %d %s\n", c.index, c.name.c_str(), c.xsec.c_str(),
           c.final_flavour.c_str(), c.rule.c_str(), c.sample_index, c.energy.c_str());

  // ---------------------------------------------------------------- output
  TFile *fout = TFile::Open(out_file.c_str(), "CREATE");
  if (!fout || fout->IsZombie() || !fout->IsWritable()) { printf("[ERROR] cannot create %s\n", out_file.c_str()); return 0; }
  fout->cd();
  TTree *out = flat->CloneTree(0);
  float Enu_rec; double POTWeight = N / nevents, SelEffWeight;
  int SampleIndex, ChannelIndex, flux_pdg_out = flux_pdg, true_bin, reco_bin;
  Bool_t isRHC = (beam == "RHC");
  Long64_t flat_entry;
  out->Branch("Enu_rec", &Enu_rec, "Enu_rec/F");
  out->Branch("POTWeight", &POTWeight, "POTWeight/D");
  out->Branch("SelEffWeight", &SelEffWeight, "SelEffWeight/D");
  out->Branch("SampleIndex", &SampleIndex, "SampleIndex/I");
  out->Branch("ChannelIndex", &ChannelIndex, "ChannelIndex/I");
  out->Branch("flux_pdg", &flux_pdg_out, "flux_pdg/I");
  out->Branch("isRHC", &isRHC, "isRHC/O");
  out->Branch("flat_entry", &flat_entry, "flat_entry/L");
  out->Branch("true_bin", &true_bin, "true_bin/I");
  out->Branch("reco_bin", &reco_bin, "reco_bin/I");

  TRandom3 rng(seed);
  long long n_sign_mismatch = 0, n_no_channel_cc = 0, n_no_channel_nc = 0, n_two = 0, n_one = 0;
  long long n_rows = 0;
  std::vector<int> match;
  for (Long64_t e = 0; e < nflat; ++e) {
    flat->GetEntry(e);
    if ((PDGnu > 0) != (flux_pdg > 0)) { ++n_sign_mismatch; continue; }
    const char ev_flav = FlavourCode(PDGnu);
    match.clear();
    for (size_t j = 0; j < chans.size(); ++j) {
      const Channel &c = chans[j];
      if (cc ? (c.xsec == "CC" && c.final_flavour == std::string(1, ev_flav))
             : (c.xsec == "NC" && c.final_flavour == nosc))
        match.push_back((int)j);
    }
    if (match.empty()) { if (cc) ++n_no_channel_cc; else ++n_no_channel_nc; continue; }
    if (match.size() == 1) ++n_one; else ++n_two;
    flat_entry = e;
    for (int j : match) {
      Channel &c = chans[j];
      ++c.n_events; c.sum_pot += POTWeight;
      const int kb = c.smear->GetXaxis()->FindFixBin(Enu_true);
      if (kb < 1 || kb > c.smear->GetNbinsX()) { ++c.n_out_of_range; continue; }
      const int k = kb - 1;
      if (!(c.colsum[k] > 0.)) { ++c.n_empty_column; continue; }
      // reco bin i = first row whose cumulative sum exceeds u; rows with a zero
      // element never qualify (their cumulative sum equals the previous one)
      const double u = rng.Uniform() * c.colsum[k];
      const std::vector<double> &cu = c.cum[k];
      int i = (int)(std::upper_bound(cu.begin(), cu.end(), u) - cu.begin());
      if (i >= (int)cu.size()) {          // u == column sum (rounding): last non-zero row
        i = (int)cu.size() - 1;
        while (i > 0 && cu[i] == cu[i - 1]) --i;
      }
      true_bin = k; reco_bin = i;
      const double lo = c.smear->GetYaxis()->GetBinLowEdge(i + 1), hi = c.smear->GetYaxis()->GetBinUpEdge(i + 1);
      Enu_rec = (float)rng.Uniform(lo, hi);
      // Enu_rec is stored as a float: keep lo <= Enu_rec < hi after the rounding
      // (a double just below hi can round up to hi; the edges are exact floats)
      if (Enu_rec >= (float)hi) Enu_rec = std::nextafter((float)hi, (float)lo);
      if (Enu_rec < (float)lo) Enu_rec = (float)lo;
      SelEffWeight = c.eff->GetBinContent(i + 1);
      SampleIndex = c.sample_index;
      ChannelIndex = c.index;
      out->Fill();
      ++c.rows; ++n_rows; c.sum_sel += POTWeight * SelEffWeight;
    }
  }

  // ---------------------------------------------------------------- summary
  printf("[INFO] events read %lld; with 2 channels %lld, with 1 channel %lld, with no channel: CC %lld, NC %lld; "
         "PDGnu sign != flux sign %lld\n", (long long)nflat, n_two, n_one, n_no_channel_cc, n_no_channel_nc, n_sign_mismatch);
  printf("[INFO] per channel: events matched, rows written, not written (Enu_true outside matrix range / empty column), "
         "sum POTWeight of matched events, sum POTWeight x SelEffWeight of rows\n");
  for (auto &c : chans)
    printf("ROWS %-22s %2d  matched %9lld  rows %9lld  not_written %lld / %lld  sumPOT %.6e  sumSel %.6e\n",
           c.name.c_str(), c.index, c.n_events, c.rows, c.n_out_of_range, c.n_empty_column, c.sum_pot, c.sum_sel);
  printf("[INFO] rows written %lld\n", n_rows);
  if (n_sign_mismatch) { printf("[ERROR] %lld events with PDGnu sign != flux sign\n", n_sign_mismatch); fout->Close(); return 0; }

  // ---------------------------------------------------------------- metadata
  out->Write();
  if (TObject *h = fin->Get("FlatTree_FLUX")) { fout->cd(); h->Clone("FlatTree_FLUX")->Write(); }
  if (TObject *h = fin->Get("FlatTree_EVT")) { fout->cd(); h->Clone("FlatTree_EVT")->Write(); }
  fout->cd();
  std::string chl, rul;
  for (auto &c : chans) {
    chl += Form("%d %s %s %s %s %s\n", c.index, c.name.c_str(), c.xsec.c_str(), c.final_flavour.c_str(),
                c.energy.c_str(), c.rule.c_str());
    if (rul.find(c.rule + " ") == std::string::npos) rul += Form("%s %d\n", c.rule.c_str(), c.sample_index);
  }
  TNamed("sample", sample.c_str()).Write();
  TNamed("beam", beam.c_str()).Write();
  TNamed("flux_pdg", Form("%d", flux_pdg)).Write();
  TNamed("channels", ("ChannelIndex channel xsec_name final_flavour energy_name rule\n" + chl).c_str()).Write();
  TNamed("rules", ("rule SampleIndex\n" + rul).c_str()).Write();
  TNamed("N", Form("%.9e", N)).Write();
  TNamed("nevents", Form("%lld", nevents)).Write();
  TNamed("POTWeight", Form("%.9e = N / nevents, interactions per event at 624 kt-MW-yr before oscillation", N / nevents)).Write();
  TNamed("seed", Form("%u", seed)).Write();
  TNamed("provenance", provenance.c_str()).Write();
  TNamed("input", flat_file.c_str()).Write();
  fout->Close();
  fin->Close();
  fr->Close();
  printf("FOLD_OK %s rows %lld\n", out_file.c_str(), n_rows);
  return 1;
}
