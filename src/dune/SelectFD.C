// =============================================================================
//  SelectFD.C - DUNE far-detector selection file: the fakerecon files of one
//  beam mode -> one tree "FakeSelect" (the tree name of the HK src/FakeSelect.C).
//
//  Usage (from the repository root; scripts/dune/select.sh builds the call):
//    root -l -b -q 'src/dune/SelectFD.C("<FHC|RHC>","<output>",
//        "<row>:<file>;<row>:<file>;...","<channels.tsv>","<rules.tsv>","<provenance>")'
//  Returns 1 on success and prints SELECT_OK (root -q exits with the return
//  value, so callers treat exit status 1 as success).
//
//  The GLoBES selection is already in the fakerecon rows (one row per event and
//  matched channel, SelEffWeight = post-smearing efficiency, src/dune/FoldResponse.C),
//  so this step drops nothing, draws no random number and computes no
//  oscillation: GUNDAM applies the oscillation dial on Enu_true. Every row of every
//  input is copied once, in manifest order.
//
//  Branches (HK names where the meaning is the same):
//    SampleIndex  I  GLoBES rule: 0 nue_app, 1 numu_dis (FHC); 2 nuebar_app,
//                    3 numubar_dis (RHC). NB: in the HK file SampleIndex is the
//                    input-file index; here it is the sample (rule), as in the
//                    fakerecon tree.
//    EventType    I  the analysis sample, as HK EventType (HK selects samples with
//                    "EventType == k"); for DUNE equal to SampleIndex. Every row is
//                    selected, so the HK value -2 (not selected) never occurs.
//    FileIndex    I  manifest row (gen/dune/samples.txt, 0-15) of the source sample
//    isRHC O, flux_pdg I, PDGnu I, cc B, Enu_true F, Enu_rec F, POTWeight D,
//    SelEffWeight D, ChannelIndex I, reco_bin I, true_bin I, flat_entry L
//  No OscWeight. Event weight before oscillation = POTWeight x SelEffWeight
//  (events at 624 kt-MW-yr).
//  TNamed: beam, channels, rules, manifest_rows, provenance (git commit and
//  sha256 of every input file).
// =============================================================================

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "TFile.h"
#include "TNamed.h"
#include "TSystem.h"
#include "TTree.h"

namespace select_fd {
std::string Trim(const std::string &s)
{
  const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
std::vector<std::string> Split(const std::string &l, char c)
{
  std::vector<std::string> v; std::stringstream ss(l); std::string t;
  while (std::getline(ss, t, c)) if (!Trim(t).empty()) v.push_back(Trim(t));
  return v;
}
std::vector<std::map<std::string, std::string>> Tsv(const std::string &f)
{
  std::vector<std::map<std::string, std::string>> rows; std::ifstream in(f.c_str()); std::string l; std::vector<std::string> h;
  while (std::getline(in, l)) {
    if (Trim(l).empty()) continue;
    std::vector<std::string> v; std::stringstream ss(l); std::string t;
    while (std::getline(ss, t, '\t')) v.push_back(Trim(t));
    if (h.empty()) { h = v; continue; }
    std::map<std::string, std::string> r; for (size_t i = 0; i < h.size() && i < v.size(); ++i) r[h[i]] = v[i];
    rows.push_back(r);
  }
  return rows;
}
} // namespace select_fd

int SelectFD(std::string beam, std::string output_file, std::string inputs, std::string channels_file,
             std::string rules_file, std::string provenance)
{
  using namespace select_fd;
  if (beam != "FHC" && beam != "RHC") { printf("[ERROR] beam must be FHC or RHC\n"); return 0; }
  if (!gSystem->AccessPathName(output_file.c_str())) { printf("[ERROR] output exists: %s\n", output_file.c_str()); return 0; }
  const bool rhc = (beam == "RHC");

  // channels and rules
  auto ch = Tsv(channels_file), ru = Tsv(rules_file);
  if (ch.empty() || ru.empty()) { printf("[ERROR] cannot read %s or %s\n", channels_file.c_str(), rules_file.c_str()); return 0; }
  std::map<std::string, std::string> rule_of;
  for (auto &r : ru) rule_of[r["channel"]] = r["rule"];
  const std::map<std::string, int> sidx = {{"nue_app", 0}, {"numu_dis", 1}, {"nuebar_app", 2}, {"numubar_dis", 3}};
  std::string chl = "ChannelIndex channel rule SampleIndex\n", rul = "rule SampleIndex\n";
  std::set<std::string> rules_seen;
  std::set<int> chan_ok;
  for (size_t j = 0; j < ch.size(); ++j) {
    if (ch[j]["beam_mode"] != beam) continue;
    const std::string rn = rule_of[ch[j]["channel"]];
    if (!sidx.count(rn)) { printf("[ERROR] channel %s has unknown rule '%s'\n", ch[j]["channel"].c_str(), rn.c_str()); return 0; }
    chan_ok.insert((int)j);
    chl += Form("%zu %s %s %d\n", j, ch[j]["channel"].c_str(), rn.c_str(), sidx.at(rn));
    if (!rules_seen.count(rn)) { rules_seen.insert(rn); rul += Form("%s %d\n", rn.c_str(), sidx.at(rn)); }
  }

  // inputs "<row>:<file>;..."
  std::vector<std::pair<int, std::string>> in;
  for (auto &tok : Split(inputs, ';')) {
    const size_t c = tok.find(':');
    if (c == std::string::npos) { printf("[ERROR] bad input token %s\n", tok.c_str()); return 0; }
    in.push_back({std::stoi(tok.substr(0, c)), tok.substr(c + 1)});
  }
  if (in.empty()) { printf("[ERROR] no input\n"); return 0; }

  TFile *fout = TFile::Open(output_file.c_str(), "CREATE");
  if (!fout || fout->IsZombie() || !fout->IsWritable()) { printf("[ERROR] cannot create %s\n", output_file.c_str()); return 0; }
  fout->cd();
  TTree *out = new TTree("FakeSelect", Form("DUNE FD selected events, %s (fakerecon rows, before oscillation)", beam.c_str()));
  int SampleIndex, EventType, FileIndex, flux_pdg, PDGnu, ChannelIndex, reco_bin, true_bin;
  Bool_t isRHC; Char_t cc; float Enu_true, Enu_rec; double POTWeight, SelEffWeight; Long64_t flat_entry;
  out->Branch("SampleIndex", &SampleIndex, "SampleIndex/I");
  out->Branch("EventType", &EventType, "EventType/I");
  out->Branch("FileIndex", &FileIndex, "FileIndex/I");
  out->Branch("isRHC", &isRHC, "isRHC/O");
  out->Branch("flux_pdg", &flux_pdg, "flux_pdg/I");
  out->Branch("PDGnu", &PDGnu, "PDGnu/I");
  out->Branch("cc", &cc, "cc/B");
  out->Branch("Enu_true", &Enu_true, "Enu_true/F");
  out->Branch("Enu_rec", &Enu_rec, "Enu_rec/F");
  out->Branch("POTWeight", &POTWeight, "POTWeight/D");
  out->Branch("SelEffWeight", &SelEffWeight, "SelEffWeight/D");
  out->Branch("ChannelIndex", &ChannelIndex, "ChannelIndex/I");
  out->Branch("reco_bin", &reco_bin, "reco_bin/I");
  out->Branch("true_bin", &true_bin, "true_bin/I");
  out->Branch("flat_entry", &flat_entry, "flat_entry/L");

  std::map<int, long long> nrow_ch; std::map<int, double> sum_ch;
  std::map<int, long long> nrow_rule; std::map<int, double> sum_rule;
  long long ntot = 0;
  std::string rows_used = "FileIndex rows file\n";
  for (auto &p : in) {
    TFile *f = TFile::Open(p.second.c_str(), "READ");
    if (!f || f->IsZombie()) { printf("[ERROR] cannot open %s\n", p.second.c_str()); fout->Close(); return 0; }
    TTree *t = (TTree *)f->Get("FlatTree_VARS");
    if (!t) { printf("[ERROR] no FlatTree_VARS in %s\n", p.second.c_str()); fout->Close(); return 0; }
    int si, fp, pn, ci, rb, tb; Bool_t ir; Char_t c; float et, er; double pw, se; Long64_t fe;
    t->SetBranchStatus("*", 0);
    for (const char *b : {"SampleIndex", "isRHC", "flux_pdg", "PDGnu", "cc", "Enu_true", "Enu_rec", "POTWeight",
                          "SelEffWeight", "ChannelIndex", "reco_bin", "true_bin", "flat_entry"}) {
      if (!t->GetBranch(b)) { printf("[ERROR] %s has no branch %s\n", p.second.c_str(), b); fout->Close(); return 0; }
      t->SetBranchStatus(b, 1);
    }
    t->SetBranchAddress("SampleIndex", &si); t->SetBranchAddress("isRHC", &ir); t->SetBranchAddress("flux_pdg", &fp);
    t->SetBranchAddress("PDGnu", &pn); t->SetBranchAddress("cc", &c); t->SetBranchAddress("Enu_true", &et);
    t->SetBranchAddress("Enu_rec", &er); t->SetBranchAddress("POTWeight", &pw); t->SetBranchAddress("SelEffWeight", &se);
    t->SetBranchAddress("ChannelIndex", &ci); t->SetBranchAddress("reco_bin", &rb); t->SetBranchAddress("true_bin", &tb);
    t->SetBranchAddress("flat_entry", &fe);
    const Long64_t n = t->GetEntries();
    for (Long64_t i = 0; i < n; ++i) {
      t->GetEntry(i);
      if ((bool)ir != rhc || !chan_ok.count(ci) || si != sidx.at(rule_of[ch[ci]["channel"]])) {
        printf("[ERROR] %s row %lld: isRHC %d, ChannelIndex %d, SampleIndex %d inconsistent with beam %s\n",
               p.second.c_str(), i, (int)ir, ci, si, beam.c_str());
        fout->Close(); return 0;
      }
      SampleIndex = si; EventType = si; FileIndex = p.first; isRHC = ir; flux_pdg = fp; PDGnu = pn; cc = c;
      Enu_true = et; Enu_rec = er; POTWeight = pw; SelEffWeight = se; ChannelIndex = ci; reco_bin = rb;
      true_bin = tb; flat_entry = fe;
      out->Fill();
      ++nrow_ch[ci]; sum_ch[ci] += pw * se; ++nrow_rule[si]; sum_rule[si] += pw * se; ++ntot;
    }
    printf("[INFO] FileIndex %2d: %9lld rows from %s\n", p.first, (long long)n, p.second.c_str());
    rows_used += Form("%d %lld %s\n", p.first, (long long)n, p.second.c_str());
    f->Close(); delete f;
    fout->cd();
  }

  out->Write();
  TNamed("beam", beam.c_str()).Write();
  TNamed("channels", chl.c_str()).Write();
  TNamed("rules", rul.c_str()).Write();
  TNamed("manifest_rows", rows_used.c_str()).Write();
  TNamed("provenance", provenance.c_str()).Write();
  TNamed("weights", "event weight before oscillation = POTWeight x SelEffWeight (events at 624 kt-MW-yr); "
                    "oscillation applied by the GUNDAM dial on Enu_true").Write();
  fout->Close();

  printf("[INFO] per channel: rows, sum POTWeight x SelEffWeight (before oscillation)\n");
  for (auto &k : nrow_ch) printf("SELCH %-22s %2d %9lld %.9e\n", ch[k.first]["channel"].c_str(), k.first, k.second, sum_ch[k.first]);
  for (auto &k : nrow_rule) printf("SELRULE SampleIndex %d %9lld %.9e\n", k.first, k.second, sum_rule[k.first]);
  printf("SELECT_OK %s rows %lld\n", output_file.c_str(), ntot);
  return 1;
}
