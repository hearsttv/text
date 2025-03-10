/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <stdlib.h>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "flashlight/lib/text/decoder/LexiconDecoder.h"
#include "flashlight/lib/text/decoder/Trie.h"
#include "flashlight/lib/text/decoder/lm/KenLM.h"
#include "flashlight/lib/text/dictionary/Defines.h"
#include "flashlight/lib/text/dictionary/Dictionary.h"
#include "flashlight/lib/text/dictionary/Utils.h"
#include "flashlight/lib/text/test/Filesystem.h"

using namespace fl::lib;
using namespace fl::lib::text;

// The token dictionary for this test defines this separator token
constexpr const char* kSepToken = "|";

/**
 * In this test, we check the output from LM, trie and decoder.
 * T, N, emissions, transitions are randomly generated.
 * Letters and words are commonly used ones in our pipeline.
 * Language model is downloaded from Librispeech website:
 * http://www.openslr.org/resources/11/3-gram.pruned.3e-7.arpa.gz
 * We pruned it so as to have much smaller size.
 */

std::vector<int> tokens2Tensor(
    const std::string& spelling,
    const Dictionary& tokenDict) {
  std::vector<int> ret;
  ret.reserve(spelling.size());
  auto tokens = splitWrd(spelling);
  for (const auto& tkn : tokens) {
    ret.push_back(tokenDict.getIndex(tkn));
  }
  ret = packReplabels(ret, tokenDict, 1);
  return ret;
}

struct Emissions {
  std::vector<float> emission; // A column-major tensor with shape T x N.
  int nFrames{0};
  int nTokens{0};
};

TEST(DecoderTest, run) {
  fs::path dataDir;
#ifdef DECODER_TEST_DATADIR
  dataDir = DECODER_TEST_DATADIR;
#endif

  /* ===================== Create Dataset ===================== */
  Emissions emissionUnit;

  // T, N
  fs::path tnPath = dataDir / "TN.bin";
  std::ifstream tnStream(tnPath, std::ios::binary | std::ios::in);
  std::vector<int> tnArray(2);
  tnStream.read((char*)tnArray.data(), 2 * sizeof(int));
  int T = tnArray[0], N = tnArray[1];
  emissionUnit.nFrames = T;
  emissionUnit.nTokens = N;
  tnStream.close();

  // Emission
  emissionUnit.emission.resize(T * N);
  fs::path emissionPath = dataDir / "emission.bin";
  std::ifstream em_stream(emissionPath, std::ios::binary | std::ios::in);
  em_stream.read((char*)emissionUnit.emission.data(), T * N * sizeof(float));
  em_stream.close();

  // Transitions
  std::vector<float> transitions(N * N);
  fs::path transitionsPath = dataDir / "transition.bin";
  std::ifstream tr_stream(transitionsPath, std::ios::binary | std::ios::in);
  tr_stream.read((char*)transitions.data(), N * N * sizeof(float));
  tr_stream.close();

  std::cout << "[Serialization] Loaded emissions [" << T << " x " << N << ']'
            << std::endl;

  /* ===================== Create Dictionary ===================== */
  auto lexicon = loadWords(dataDir / "words.lst");
  Dictionary tokenDict(dataDir / "letters.lst");
  tokenDict.addEntry("<1>"); // replabel emulation
  auto wordDict = createWordDict(lexicon);

  std::cout << "[Dictionary] Number of words: " << wordDict.indexSize()
            << std::endl;

  /* ===================== Decode ===================== */
  /* -------- Build Language Model --------*/
  auto lm = std::make_shared<KenLM>(dataDir / "lm.arpa", wordDict);
  std::cout << "[Decoder] LM constructed." << std::endl;

  std::vector<std::string> sentence{"the", "cat", "sat", "on", "the", "mat"};
  auto inState = lm->start(0);
  float totalScore = 0, lmScore = 0;
  std::vector<float> lmScoreTarget{
      -1.05971, -4.19448, -3.33383, -2.76726, -1.16237, -4.64589};
  for (int i = 0; i < sentence.size(); i++) {
    const auto& word = sentence[i];
    std::tie(inState, lmScore) = lm->score(inState, wordDict.getIndex(word));
    ASSERT_NEAR(lmScore, lmScoreTarget[i], 1e-5);
    totalScore += lmScore;
  }
  std::tie(inState, lmScore) = lm->finish(inState);
  totalScore += lmScore;
  ASSERT_NEAR(totalScore, -19.5123, 1e-5);

  /* -------- Build Trie --------*/
  int silIdx = tokenDict.getIndex(kSepToken);
  int blankIdx = -1;
  int unkIdx = wordDict.getIndex(kUnkToken);
  auto trie = std::make_shared<Trie>(tokenDict.indexSize(), silIdx);
  auto startState = lm->start(false);

  // Insert words
  for (const auto& it : lexicon) {
    const std::string& word = it.first;
    int usrIdx = wordDict.getIndex(word);
    float score = -1;
    LMStatePtr dummyState;
    std::tie(dummyState, score) = lm->score(startState, usrIdx);

    for (const auto& tokens : it.second) {
      auto tokensTensor = tkn2Idx(tokens, tokenDict, 1);
      trie->insert(tokensTensor, usrIdx, score);
    }
  }
  std::cout << "[Decoder] Trie planted." << std::endl;

  // Smearing
  trie->smear(SmearingMode::MAX);
  std::cout << "[Decoder] Trie smeared." << std::endl;

  std::vector<float> trieScoreTarget{
      -1.05971, -2.87742, -2.64553, -3.05081, -1.05971, -3.08968};
  for (int i = 0; i < sentence.size(); i++) {
    auto word = sentence[i];
    auto wordTensor = tokens2Tensor(word, tokenDict);
    auto node = trie->search(wordTensor);
    ASSERT_NEAR(node->maxScore, trieScoreTarget[i], 1e-5);
  }

  /* -------- Build Decoder --------*/
  LexiconDecoderOptions decoderOpt{
      .beamSize = 2500, // beamsize
      .beamSizeToken = 25000, // beamsizetoken
      .beamThreshold = 100.0, // beamthreshold
      .lmWeight = 2.0, // lmweight
      .wordScore = 2.0, // lexiconcore
      .unkScore = -std::numeric_limits<float>::infinity(), // unkscore
      .silScore = -1, // silscore
      .logAdd = false, // logadd
      .criterionType = CriterionType::ASG};

  LexiconDecoder decoder(
      decoderOpt, trie, wordDict, lm, silIdx, blankIdx, unkIdx, transitions, false);
  std::cout << "[Decoder] Decoder constructed." << std::endl;

  /* -------- Run --------*/
  auto emission = emissionUnit.emission;

  std::vector<float> score;
  std::vector<std::vector<int>> wordPredictions;
  std::vector<std::vector<int>> letterPredictions;

  auto results = decoder.decode(emission.data(), T, N);

  int n_hyp = results.size();

  ASSERT_EQ(n_hyp, 16); // only one with nice ending

  for (int i = 0; i < std::min(n_hyp, 5); i++) {
    std::cout << results[i].score << std::endl;
  }

  std::vector<float> hypScoreTarget{
      -284.0998, -284.108, -284.119, -284.127, -284.296};
  for (int i = 0; i < std::min(n_hyp, 5); i++) {
    ASSERT_NEAR(results[i].score, hypScoreTarget[i], 1e-3);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
