#include "Json.h"
#include "LichessProtocol.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
    int failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::fprintf(stderr, "FAILED: %s\n", what);
            ++failures;
        }
    }

    void checkJson() {
        std::string error;
        auto parsed = Json::parse(
            R"({"type":"gameFull","id":"abcd1234","initialFen":"startpos","speed":"rapid",)"
            R"("white":{"id":"me","name":"Me","rating":1500},)"
            R"("black":{"aiLevel":3},"clock":{"initial":600000,"increment":5000},)"
            R"("state":{"type":"gameState","moves":"e2e4 e7e5","wtime":598000,"btime":600000,)"
            R"("winc":5000,"binc":5000,"status":"started","bdraw":true},)"
            R"("text":"café \"quoted\"\n","list":[1,-2.5e1,true,null]})",
            &error);
        expect(bool(parsed), "valid JSON parses");
        if (!parsed)
            return;
        const Json &event = *parsed;
        expect(event["type"].str() == "gameFull", "string member");
        expect(event["text"].str() == "caf\xc3\xa9 \"quoted\"\n", "escapes and UTF-8");
        expect(event["list"].items().size() == 4 && event["list"].items()[1].num() == -25,
               "array and exponent");
        expect(event["missing"]["deeper"].str("x") == "x", "missing members default");
        auto full = parseGameFull(event);
        expect(full.id == "abcd1234" && full.speed == "rapid", "gameFull id and speed");
        expect(full.white.name == "Me" && full.white.rating == 1500, "white player");
        expect(full.black.aiLevel == 3 && full.black.name == "Lichess AI level 3", "AI player");
        expect(full.clockInitial == 600000 && full.clockIncrement == 5000, "clock milliseconds");
        expect(full.state.moves == "e2e4 e7e5" && full.state.wtime == 598000 &&
                   full.state.bdraw && !full.state.wdraw,
               "embedded gameState");
        expect(!Json::parse("{\"a\":}"), "malformed JSON rejected");
        expect(!Json::parse("{\"a\":1} x"), "trailing text rejected");
        expect(!Json::parse(std::string(100, '[')), "deep nesting rejected");

        auto challenge = Json::parse(
            R"({"id":"ch1","challenger":{"id":"x","name":"Opponent"},"rated":false,)"
            R"("speed":"rapid","color":"random","direction":"in",)"
            R"("timeControl":{"type":"clock","limit":600,"increment":0,"show":"10+0"}})");
        auto parsedChallenge = parseChallenge(*challenge);
        expect(parsedChallenge.id == "ch1" && parsedChallenge.challenger == "Opponent" &&
                   parsedChallenge.timeControl == "10+0" && parsedChallenge.incoming,
               "challenge fields");
    }

    void checkUci() {
        BCGame game;
        bcGameInit(&game);
        BCMove move;
        expect(resolveUci(game, "e2e4", move) && uciMove(game, move) == "e2e4", "pawn push");
        expect(!resolveUci(game, "e2e5", move), "illegal move rejected");
        expect(!resolveUci(game, "z9e4", move) && !resolveUci(game, "e2e4x", move),
               "malformed UCI rejected");

        // Both castling notations, then the move list as Lichess streams it.
        for (const char *castle : {"e1g1", "e1h1"}) {
            std::string moves = "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 ";
            moves += castle;
            auto line = buildLichessLine("startpos", moves);
            expect(line.error.empty() && line.moves.size() == 7, "castling line builds");
            BCGame before = line.start;
            for (size_t i = 0; i + 1 < line.moves.size(); ++i)
                bcGameApply(&before, line.moves[i], nullptr);
            expect(uciMove(before, line.moves.back()) == "e1g1", "castling encodes as e1g1");
        }

        // Promotion needs its piece letter, and round-trips.
        auto promotion = buildLichessLine("k7/7P/8/8/8/8/8/K7 w - - 0 1", "h7h8n");
        expect(promotion.error.empty() && promotion.moves.size() == 1 &&
                   promotion.moves[0].piece == 5,
               "underpromotion from FEN");
        if (promotion.error.empty())
            expect(uciMove(promotion.start, promotion.moves[0]) == "h7h8n", "promotion UCI");
        auto bare = buildLichessLine("k7/7P/8/8/8/8/8/K7 w - - 0 1", "h7h8");
        expect(!bare.error.empty(), "promotion without piece rejected");
        auto bad = buildLichessLine("startpos", "e2e4 e7e5 e1e3");
        expect(bad.error == "Illegal move from Lichess: e1e3" && bad.moves.size() == 2,
               "illegal streamed move reported");
        expect(!buildLichessLine("not a fen", "").error.empty(), "bad FEN reported");
        expect(splitUciMoves("  e2e4  e7e5 ").size() == 2 && splitUciMoves("").empty(),
               "move list splitting");
    }

    void checkPolicy() {
        using P = LichessAnimationPolicy;
        using M = LichessMoveAnimation;
        expect(chooseLichessAnimation(P::full, "blitz", true, 1000, true) == M::full,
               "Full always animates");
        expect(chooseLichessAnimation(P::boardOnly, "classical", true, 600000, false) == M::none,
               "Board Only never animates");
        expect(chooseLichessAnimation(P::fast, "classical", true, 600000, false) == M::fast &&
                   chooseLichessAnimation(P::fast, "classical", true, 600000, true) == M::none,
               "Fast: quiet moves fast, captures instant");
        expect(chooseLichessAnimation(P::automatic, "correspondence", false, 0, true) == M::full,
               "Auto: correspondence animates fully");
        expect(chooseLichessAnimation(P::automatic, "classical", true, 1200000, true) == M::full,
               "Auto: classical animates fully");
        expect(chooseLichessAnimation(P::automatic, "rapid", true, 300000, false) == M::fast &&
                   chooseLichessAnimation(P::automatic, "rapid", true, 300000, true) == M::none,
               "Auto: rapid is fast");
        expect(chooseLichessAnimation(P::automatic, "blitz", true, 300000, false) == M::none,
               "Auto: blitz shows moves at once");
        expect(chooseLichessAnimation(P::automatic, "classical", true, 59000, false) == M::none,
               "Auto: under a minute shows moves at once");
    }

    void checkText() {
        expect(formatLichessClock(598000) == "9:58", "clock minutes");
        expect(formatLichessClock(3723000) == "1:02:03", "clock hours");
        expect(formatLichessClock(7400) == "0:07.4", "clock tenths");
        expect(formatLichessClock(-5) == "0:00.0", "clock below zero");
        LichessGameState state;
        state.status = "started";
        expect(lichessEndMessage(state, "white", "Opp").empty(), "running game has no message");
        state.status = "mate";
        state.winner = "white";
        expect(lichessEndMessage(state, "white", "Opp") == "You win by checkmate.", "win");
        expect(lichessEndMessage(state, "black", "Opp") == "Opp wins by checkmate.", "loss");
        state.status = "draw";
        state.winner.clear();
        expect(lichessEndMessage(state, "white", "Opp") == "Game drawn.", "draw");
        state.status = "aborted";
        expect(lichessEndMessage(state, "white", "Opp") == "Game aborted.", "abort");
        expect(formEscape("1. e4 e5 & =") == "1.+e4+e5+%26+%3D", "form escaping");
        int seekable = 0;
        for (const auto &control : lichessTimeControls())
            seekable += control.seekable;
        expect(seekable == int(lichessTimeControls().size()) - 1, "only blitz is not seekable");
    }
}

int main() {
    checkJson();
    checkUci();
    checkPolicy();
    checkText();
    if (failures)
        return EXIT_FAILURE;
    std::puts("Lichess protocol checks passed");
    return EXIT_SUCCESS;
}
