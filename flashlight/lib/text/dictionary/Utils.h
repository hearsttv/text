/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <fstream>
#include <memory>

#include "flashlight/lib/text/dictionary/Dictionary.h"

namespace fl {
namespace lib {
namespace text {

using LexiconMap =
    std::unordered_map<std::string, std::vector<std::vector<std::string>>>;

Dictionary createWordDict(const LexiconMap& lexicon);

Dictionary createCutomVocabularyDict(const LexiconMap& customVocab, const float weightFactor);
    
LexiconMap loadWords(const std::string& filename, int maxWords = -1);

// split word into tokens abc -> {"a", "b", "c"}
// Works with ASCII, UTF-8 encodings
std::vector<std::string> splitWrd(const std::string& word);

/**
 * Pack a token sequence by replacing consecutive repeats with replabels,
 * e.g. "abbccc" -> "ab1c2". The tokens "1", "2", ..., `to_string(maxReps)`
 * must already be in `dict`.
 */
std::vector<int> packReplabels(
    const std::vector<int>& tokens,
    const Dictionary& dict,
    int maxReps);

/**
 * Unpack a token sequence by replacing replabels with repeated tokens,
 * e.g. "ab1c2" -> "abbccc". The tokens "1", "2", ..., `to_string(maxReps)`
 * must already be in `dict`.
 */
std::vector<int> unpackReplabels(
    const std::vector<int>& tokens,
    const Dictionary& dict,
    int maxReps);

/**
 * Map the spelling of a word to letter indices as defined by a Dictionary
 * with a maximum number of replabels as defined.
 */
std::vector<int> tkn2Idx(
    const std::vector<std::string>& spelling,
    const fl::lib::text::Dictionary& tokenDict,
    int maxReps);

//usage example: std::cout << string_format("value = %d", 202412);
template<typename ... Args>
std::string dict_string_format( const std::string& format, Args ... args )
{
    size_t size = snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    std::unique_ptr<char[]> buf( new char[ size ] ); 
    snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}
    
//usage example: write_log_file("value = %d", 202412);
template<typename ... Args>
void dict_write_log_file(const std::string& msg, Args ... args) {
  std::ofstream logFile;
  logFile.open ("/Users/fabricio/Public/Dictionary_Log.txt", std::ofstream::out | std::ofstream::app);  
  try {
    logFile << dict_string_format(msg, args...) << "\n";
  }
  catch (std::string st) {
    std::cerr << st;
  } 
  logFile.close();        
}

} // namespace text
} // namespace lib
} // namespace fl
