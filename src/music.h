// ===========================================================================
//  music.h  -  background music
//
//  HOW THE MUSIC IS PLAYED
//  Windows has a built-in media player that programs can drive with plain
//  text commands, called MCI (Media Control Interface). It already knows how
//  to decode MP3, so no audio library is needed at all - just one function,
//  mciSendString, and the system library winmm (linked with -lwinmm).
//
//  The commands read almost like English:
//
//      open "file.mp3" type mpegvideo alias track0   load a file, name it
//      play track0 from 0 repeat                     start from the top, loop
//      pause track0                                  stop, remembering where
//      play track0 repeat                            carry on from there
//      setaudio track0 volume to 700                 volume, 0..1000
//      close all                                     release everything
//
//  "mpegvideo" is MCI's name for its general media player (it handles MP3
//  too). An "alias" is a short name for an opened file, so later commands
//  do not have to repeat the whole path.
//
//  MCI plays in the background on its own. mciSendString returns
//  immediately, so the render loop never waits for the music.
//
//  THE TRACKS
//  All three are opened once at startup, so switching is instant - no file
//  is read in the middle of a frame. Nightcall is track 0, the default.
// ===========================================================================

#pragma once

// windows.h, by default, defines macros called min and max. Those would
// break every std::min / std::max in main.cpp, so NOMINMAX turns them off.
// WIN32_LEAN_AND_MEAN skips the rarely-used parts of windows.h, which also
// means the multimedia header has to be included by hand.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <string>
#include <vector>
#include <iostream>

struct Track {
    std::string title;     // shown in the window title
    std::string file;      // file name inside the music folder
    bool        loaded;    // false if the file could not be opened
};

struct MusicPlayer {
    std::vector<Track> tracks;
    int  current = 0;      // which track is selected
    bool playing = false;  // is music on right now?
    int  volume  = 350;    // 0..1000. Kept low: this is background music,
                           // it should sit under the scene, not over it.
};

// ---------------------------------------------------------------------------
//  Send one MCI command. On failure, MCI can describe the problem in words,
//  which is printed so a missing file is obvious instead of just silent.
// ---------------------------------------------------------------------------
inline bool mci(const std::string& command, bool reportErrors = true) {
    MCIERROR err = mciSendStringA(command.c_str(), NULL, 0, NULL);
    if (err != 0 && reportErrors) {
        char msg[256];
        mciGetErrorStringA(err, msg, sizeof(msg));
        std::cout << "MUSIC: \"" << command << "\" failed: " << msg << std::endl;
    }
    return err == 0;
}

inline std::string trackAlias(int i) { return "track" + std::to_string(i); }

// ---------------------------------------------------------------------------
//  The folder the .exe lives in.
//
//  A path like "assets/music/..." is looked up from the CURRENT folder, which
//  depends on how the program was started (double-clicked, run from VS Code,
//  run from another folder). Asking Windows where the .exe itself is makes
//  the music load no matter how it was launched.
// ---------------------------------------------------------------------------
inline std::string exeFolder() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string s(path, n);
    size_t slash = s.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string(".") : s.substr(0, slash);
}

// ---------------------------------------------------------------------------
//  What format is a file REALLY in?
//
//  The name ending (.mp3) is only a label - a file keeps its real format
//  whatever it is called. Every format starts with its own "signature" bytes,
//  so reading the first few tells the truth. This matters because an audio
//  editor can save as M4A while keeping the .mp3 name, and Windows' player
//  then refuses the file with a vague error. Checking here turns that into a
//  clear message.
// ---------------------------------------------------------------------------
inline std::string audioFormatProblem(const std::string& path) {
    unsigned char b[12] = { 0 };
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "the file is missing";
    size_t n = fread(b, 1, sizeof(b), f);
    fclose(f);
    if (n < 12) return "the file is too small to be audio";

    if (b[4] == 'f' && b[5] == 't' && b[6] == 'y' && b[7] == 'p')
        return "it is really an MP4/M4A (AAC) file with an .mp3 name - export it as MP3";
    if (b[0] == 'O' && b[1] == 'g' && b[2] == 'g' && b[3] == 'S')
        return "it is really an OGG file - export it as MP3";
    if (b[0] == 'f' && b[1] == 'L' && b[2] == 'a' && b[3] == 'C')
        return "it is really a FLAC file - export it as MP3";
    return "";   // MP3 ("ID3" or a raw frame) or WAV ("RIFF"): both play fine
}

// ---------------------------------------------------------------------------
//  Open every track. Does not start playing.
// ---------------------------------------------------------------------------
inline void musicInit(MusicPlayer& m) {
    m.tracks = {
        { "Kavinsky - Nightcall",           "Kavinsky - Nightcall.mp3",           false },
        { "Skrillex - Bangarang",           "Skrillex - Bangarang.mp3",           false },
        { "Zomboy - City 2 Belle Humble",   "Zomboy - City 2 Belle Humble.mp3",   false },
    };

    std::string folder = exeFolder() + "\\assets\\music\\";
    int ok = 0;
    for (size_t i = 0; i < m.tracks.size(); ++i) {
        std::string problem = audioFormatProblem(folder + m.tracks[i].file);
        if (!problem.empty()) {
            std::cout << "MUSIC: cannot play \"" << m.tracks[i].file << "\": " << problem << std::endl;
            continue;
        }
        // The quotes around the path matter: the file names contain spaces,
        // and without quotes MCI would read only up to the first space.
        std::string cmd = "open \"" + folder + m.tracks[i].file +
                          "\" type mpegvideo alias " + trackAlias((int)i);
        m.tracks[i].loaded = mci(cmd);
        if (m.tracks[i].loaded) {
            mci("setaudio " + trackAlias((int)i) + " volume to " + std::to_string(m.volume));
            ++ok;
        }
    }
    std::cout << "Music: " << ok << " of " << m.tracks.size()
              << " tracks loaded from " << folder << std::endl;
}

// Start track i from the beginning, looping. Stops whatever was playing.
inline void musicPlay(MusicPlayer& m, int i) {
    if (m.tracks.empty()) return;
    if (m.playing && m.tracks[m.current].loaded)
        mci("stop " + trackAlias(m.current), false);

    m.current = i;
    if (!m.tracks[i].loaded) { m.playing = false; return; }

    // "from 0" rewinds to the start; "repeat" loops forever.
    m.playing = mci("play " + trackAlias(i) + " from 0 repeat");
}

// Music on / off. Off PAUSES rather than stops, so turning it back on
// carries on from the same place in the song.
inline void musicToggle(MusicPlayer& m) {
    if (m.tracks.empty() || !m.tracks[m.current].loaded) return;
    std::string a = trackAlias(m.current);
    if (m.playing) {
        mci("pause " + a);
        m.playing = false;
    } else {
        // "play" with no "from" continues from the paused position.
        m.playing = mci("play " + a + " repeat");
    }
}

// Skip to the next track, wrapping round after the last. It always starts
// playing - pressing "next" is a request to hear something.
inline void musicNext(MusicPlayer& m) {
    if (m.tracks.empty()) return;
    int next = (m.current + 1) % (int)m.tracks.size();
    musicPlay(m, next);
}

inline std::string musicStatus(const MusicPlayer& m) {
    if (m.tracks.empty() || !m.tracks[m.current].loaded) return "no music";
    return (m.playing ? "Playing: " : "Paused: ") + m.tracks[m.current].title;
}

inline void musicShutdown(MusicPlayer&) {
    mci("close all", false);
}
