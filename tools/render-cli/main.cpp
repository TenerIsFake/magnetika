// render-cli: replays an engine-call trace (written by reference/render.mjs)
// through the C++ engine and writes one 32-bit float stereo WAV per scenario.
//
//   render-cli <trace.txt> <out-dir> [--armed]
//
// --armed turns on "coils always armed", which the reference does not have; it
// is for listening, not for comparison.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "magnetika/synth.hpp"

using namespace magnetika;

namespace {

constexpr int kBlock = 128;
constexpr double kSampleRate = 48000.0;

[[noreturn]] void fail(const std::string& msg, int line) {
    std::cerr << "trace line " << line << ": " << msg << "\n";
    std::exit(2);
}

struct Call {
    long long at = 0;
    std::string op;
    std::vector<std::string> args;
    int line = 0;
};

struct Scenario {
    std::string name;
    long long frames = 0;
    std::vector<Call> calls;
};

std::vector<Scenario> readTrace(const std::string& path) {
    std::ifstream in(path);
    if (!in) { std::cerr << "cannot open " << path << "\n"; std::exit(2); }
    std::vector<Scenario> out;
    std::string text;
    int line = 0;
    Scenario* cur = nullptr;
    while (std::getline(in, text)) {
        ++line;
        if (text.empty() || text[0] == '#') continue;
        std::istringstream ss(text);
        std::string first;
        ss >> first;
        if (first == "scenario") {
            out.emplace_back();
            cur = &out.back();
            ss >> cur->name >> cur->frames;
            if (cur->name.empty() || cur->frames <= 0) fail("bad scenario header", line);
        } else if (first == "end") {
            cur = nullptr;
        } else {
            if (!cur) fail("call outside a scenario", line);
            Call c;
            c.line = line;
            c.at = std::stoll(first);
            ss >> c.op;
            for (std::string a; ss >> a;) c.args.push_back(a);
            cur->calls.push_back(std::move(c));
        }
    }
    return out;
}

double num(const std::string& s, int line) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') fail("not a number: " + s, line);
    return v;
}

Instrument instrument(const std::string& s, int line) {
    if (s == "glass") return Instrument::Glass;
    if (s == "bars") return Instrument::Bars;
    fail("unknown instrument: " + s, line);
}

void apply(Synth& synth, const Call& c) {
    if (c.op == "setParams") {
        if (c.args.empty()) fail("setParams needs an instrument", c.line);
        std::vector<ParamValue> vals;
        for (size_t i = 1; i < c.args.size(); ++i) {
            const auto eq = c.args[i].find('=');
            if (eq == std::string::npos) fail("expected key=value: " + c.args[i], c.line);
            const auto id = paramFromName(c.args[i].substr(0, eq));
            if (!id) fail("unknown parameter: " + c.args[i].substr(0, eq), c.line);
            vals.push_back({*id, num(c.args[i].substr(eq + 1), c.line)});
        }
        synth.setParams(instrument(c.args[0], c.line), vals.data(), static_cast<int>(vals.size()));
    } else if (c.op == "setPerf") {
        std::vector<PerfValue> vals;
        for (const auto& a : c.args) {
            const auto eq = a.find('=');
            if (eq == std::string::npos) fail("expected key=value: " + a, c.line);
            const auto id = perfFromName(a.substr(0, eq));
            if (!id) fail("unknown perf key: " + a.substr(0, eq), c.line);
            vals.push_back({*id, num(a.substr(eq + 1), c.line)});
        }
        synth.setPerf(vals.data(), static_cast<int>(vals.size()));
    } else if (c.op == "setInst") {
        if (c.args.size() != 1) fail("setInst takes one argument", c.line);
        synth.setInst(instrument(c.args[0], c.line));
    } else if (c.op == "noteOn") {
        if (c.args.size() != 2) fail("noteOn takes midi and velocity", c.line);
        synth.noteOn(static_cast<int>(num(c.args[0], c.line)), num(c.args[1], c.line));
    } else if (c.op == "noteOff") {
        if (c.args.size() != 1) fail("noteOff takes midi", c.line);
        synth.noteOff(static_cast<int>(num(c.args[0], c.line)));
    } else {
        fail("unknown call: " + c.op, c.line);
    }
}

void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto n = static_cast<std::uint32_t>(L.size()), data = 8 * n;
    f.write("RIFF", 4); u32(36 + data); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(3); u16(2);
    u32(static_cast<std::uint32_t>(kSampleRate)); u32(static_cast<std::uint32_t>(kSampleRate) * 8); u16(8); u16(32);
    f.write("data", 4); u32(data);
    for (std::uint32_t i = 0; i < n; ++i) {
        f.write(reinterpret_cast<const char*>(&L[i]), 4);
        f.write(reinterpret_cast<const char*>(&R[i]), 4);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: render-cli <trace.txt> <out-dir> [--armed]\n";
        return 2;
    }
    const bool armed = argc > 3 && std::string(argv[3]) == "--armed";
    const auto scenarios = readTrace(argv[1]);
    for (const auto& sc : scenarios) {
        Synth synth(kSampleRate);
        synth.setCoilsArmed(armed);
        std::vector<float> L(static_cast<size_t>(sc.frames)), R(static_cast<size_t>(sc.frames));
        size_t k = 0;
        for (long long i = 0; i < sc.frames; i += kBlock) {
            while (k < sc.calls.size() && sc.calls[k].at <= i) apply(synth, sc.calls[k++]);
            synth.render(&L[static_cast<size_t>(i)], &R[static_cast<size_t>(i)], kBlock);
        }
        writeWav(std::string(argv[2]) + "/" + sc.name + ".wav", L, R);
        std::cout << sc.name << "\n";
    }
    return 0;
}
