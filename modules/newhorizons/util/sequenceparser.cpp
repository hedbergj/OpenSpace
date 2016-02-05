/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2016                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/newhorizons/util/sequenceparser.h>

#include <openspace/engine/openspaceengine.h>
#include <openspace/util/spicemanager.h>
#include <modules/newhorizons/util/decoder.h>

#include <ghoul/logging/logmanager.h>

namespace {
	const std::string _loggerCat = "SequenceParser";
	const std::string keyTranslation = "DataInputTranslation";

    const std::string PlaybookIdentifierName = "Playbook";
}

namespace openspace {

std::map<std::string, ImageSubset> SequenceParser::getSubsetMap(){
    return _subsetMap;
}
std::vector<std::pair<std::string, TimeRange>> SequenceParser::getIstrumentTimes(){
    return _instrumentTimes;
}
std::vector<std::pair<double, std::string>> SequenceParser::getTargetTimes(){
    return _targetTimes;
}
std::vector<double> SequenceParser::getCaptureProgression(){
    return _captureProgression;
};


template <typename T>
void writeToBuffer(std::vector<char>& buffer, size_t& currentWriteLocation, T value) {
    if ((currentWriteLocation + sizeof(T)) > buffer.size())
        buffer.resize(2 * buffer.size());

    std::memmove(buffer.data() + currentWriteLocation, reinterpret_cast<const void*>(&value), sizeof(T));
    currentWriteLocation += sizeof(T);
}

template <>
void writeToBuffer<std::string>(std::vector<char>& buffer, size_t& currentWriteLocation, std::string value) {
    if ((currentWriteLocation + sizeof(uint8_t) + value.size()) > buffer.size())
        buffer.resize(2 * buffer.size());

    uint8_t length = static_cast<uint8_t>(value.size());
    std::memcpy(buffer.data() + currentWriteLocation, &length, sizeof(uint8_t));
    currentWriteLocation += sizeof(uint8_t);

    std::memmove(buffer.data() + currentWriteLocation, value.data(), length);
    currentWriteLocation += length;
}

void SequenceParser::sendPlaybookInformation(const std::string& name) {
    std::string fullName = PlaybookIdentifierName + "_" + name;
    _messageIdentifier = OsEng.networkEngine().identifier(fullName);

    std::vector<char> buffer(1024);
    size_t currentWriteLocation = 0;

    // Protocol:
    // 4 bytes: Total number of bytes sent
    // 1 byte : Number of Targets (i)
    // i times: 1 byte (id), 1 byte (length j of name), j bytes (name)
    // 1 byte : Number of Instruments (i)
    // i times: 1 byte (id), 1 byte (length j of name), j bytes (name)
    // 4 byte: Number (n) of images
    // n times: 8 byte (beginning time), 8 byte (ending time), 1 byte (target id), 2 byte (instrument id)

    std::map<std::string, uint8_t> targetMap;
    uint8_t currentTargetId = 0;
    for (auto target : _subsetMap) {
        if (targetMap.find(target.first) == targetMap.end())
            targetMap[target.first] = currentTargetId++;
    }

    std::map<std::string, uint16_t> instrumentMap;
    uint16_t currentInstrumentId = 1;
    for (auto target : _subsetMap) {
        for (auto image : target.second._subset) {
            for (auto instrument : image.activeInstruments) {
                if (instrumentMap.find(instrument) == instrumentMap.end()) {
                    instrumentMap[instrument] = currentInstrumentId;
                    currentInstrumentId = currentInstrumentId << 1;
                }
            }
        }
    }

    writeToBuffer(buffer, currentWriteLocation, uint8_t(targetMap.size()));
    for (const std::pair<std::string, uint8_t>& p : targetMap) {
        writeToBuffer(buffer, currentWriteLocation, p.second);
        writeToBuffer(buffer, currentWriteLocation, p.first);
    }

    writeToBuffer(buffer, currentWriteLocation, uint8_t(instrumentMap.size()));
    for (const std::pair<std::string, uint16_t>& p : instrumentMap) {
        writeToBuffer(buffer, currentWriteLocation, p.second);
        writeToBuffer(buffer, currentWriteLocation, p.first);
    }

    uint32_t allImages = 0;
    for (auto target : _subsetMap)
        allImages += static_cast<uint32_t>(target.second._subset.size());
    writeToBuffer(buffer, currentWriteLocation, allImages);

    for (auto target : _subsetMap){
        for (auto image : target.second._subset){
            writeToBuffer(buffer, currentWriteLocation, image.startTime);
            writeToBuffer(buffer, currentWriteLocation, image.stopTime);

            std::string timeBegin = SpiceManager::ref().dateFromEphemerisTime(image.startTime);
            std::string timeEnd = SpiceManager::ref().dateFromEphemerisTime(image.stopTime);

            writeToBuffer(buffer, currentWriteLocation, timeBegin);
            writeToBuffer(buffer, currentWriteLocation, timeEnd);

            uint8_t targetId = targetMap[target.first];
            writeToBuffer(buffer, currentWriteLocation, targetId);
            uint16_t totalInstrumentId = 0;
            if (image.activeInstruments.empty()) {
                LERROR("Image had no active instruments");
            }

            for (auto instrument : image.activeInstruments) {
                uint16_t thisInstrumentId = instrumentMap[instrument];
                totalInstrumentId |= thisInstrumentId;
            }
            writeToBuffer(buffer, currentWriteLocation, totalInstrumentId);
        }
    }

    union {
        uint32_t value;
        std::array<char, sizeof(uint32_t)> data;
    } sizeBuffer;
    sizeBuffer.value = static_cast<uint32_t>(currentWriteLocation);
    buffer.insert(buffer.begin(), sizeBuffer.data.begin(), sizeBuffer.data.end());
    currentWriteLocation += sizeof(uint32_t);

    buffer.resize(currentWriteLocation);

    //OsEng.networkEngine()->publishMessage(PlaybookIdentifier, buffer);
    OsEng.networkEngine().setInitialConnectionMessage(_messageIdentifier, buffer);
}

}