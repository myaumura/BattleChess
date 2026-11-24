#include "ApplicationInternal.h"
#include "LichessClient.h"
#include "MacBitmapFont.h"
#include "PgnExport.h"
#include "adjudication.h"
#include "presentation.h"
#include <cstdlib>
#include <cstring>

/* Native Lichess Board API integration; no original entry point. Network transfers run
 * in LichessClient; everything here runs on the SDL thread. The recovered engine never
 * moves or advises for the player's account: while a Lichess game runs, computer play,
 * hints and local-only commands are disabled. API facts follow the published OpenAPI
 * specification (lichess-org/api). */

#ifndef BC_HAVE_LICHESS
#define BC_HAVE_LICHESS 0
#endif

namespace {
    constexpr const char *kDefaultBase = "https://lichess.org";
    // Generic client name; it identifies the application, never the user.
    constexpr const char *kUserAgent = "BattleChess-native/1.0 (Lichess Board API client)";
    constexpr const char *kTokenPage =
        "https://lichess.org/account/oauth/token/create?scopes%5B%5D=board:play"
        "&scopes%5B%5D=challenge:read&scopes%5B%5D=challenge:write&description=Battle+Chess";
    constexpr Uint64 kEventsRetry = 5000, kGameRetry = 2000;

    std::string baseUrl() {
        const char *override = std::getenv("BC_LICHESS_URL");
        return override && *override ? override : kDefaultBase;
    }

    std::string errorText(const LichessMessage &message) {
        if (!message.error.empty())
            return message.error;
        if (auto json = Json::parse(message.body))
            if (auto text = (*json)["error"].str(); !text.empty())
                return text;
        return "HTTP " + std::to_string(message.status);
    }

    bool isCaptureIn(const BCGame &before, const BCMove &move) {
        const auto &mover = before.position.board[move.from];
        return before.position.board[move.to].piece ||
               (mover.piece == 6 && (move.from & 7) != (move.to & 7));
    }
}

LichessState::LichessState() = default;
LichessState::~LichessState() = default;
LichessState &LichessState::operator=(LichessState &&) noexcept = default;

bool Application::lichessAvailable() const {
    return BC_HAVE_LICHESS || lichess.offline;
}

bool Application::lichessMenuVisible() const {
    return lichessAvailable() && !editing && !modem.connected();
}

void Application::lichessRequest(const std::string &tag, const std::string &method,
                                 const std::string &path, const std::string &form) {
    if (lichess.offline) {
        lichess.outbox.push_back(method + " " + path + (form.empty() ? "" : " " + form));
        return;
    }
#if BC_HAVE_LICHESS
    if (!lichess.client)
        lichess.client = std::make_unique<LichessClient>(baseUrl(), kUserAgent);
    lichess.client->setToken(lichess.token);
    lichess.client->request(tag, method, path, form);
#else
    (void)tag;
#endif
}

void Application::lichessOpenStream(const std::string &name, const std::string &method,
                                    const std::string &path, const std::string &form) {
    if (lichess.offline) {
        lichess.outbox.push_back("STREAM " + name + " " + method + " " + path +
                                 (form.empty() ? "" : " " + form));
        return;
    }
#if BC_HAVE_LICHESS
    if (!lichess.client)
        lichess.client = std::make_unique<LichessClient>(baseUrl(), kUserAgent);
    lichess.client->setToken(lichess.token);
    lichess.client->openStream(name, method, path, form);
#endif
}

void Application::lichessCloseStream(const std::string &name) {
    if (lichess.offline) {
        lichess.outbox.push_back("CLOSE " + name);
        return;
    }
#if BC_HAVE_LICHESS
    if (lichess.client)
        lichess.client->closeStream(name);
#endif
}

static void openUrl(LichessState &lichess, const std::string &url) {
    if (lichess.offline)
        lichess.outbox.push_back("OPEN " + url);
    else
        SDL_OpenURL(url.c_str());
}

// ---------------------------------------------------------------- menu

const char *Application::lichessMenuLabel(int item) const {
    static std::string labels[kLichessItemCount];
    std::string &label = labels[item];
    switch (item) {
    case kLichessConnect:
        label = lichess.connected    ? "Disconnect"
                : lichess.connecting ? "Connecting..."
                                     : "Connect...";
        break;
    case kLichessForget: label = "Forget Lichess Token"; break;
    case kLichessGames: label = "Games..."; break;
    case kLichessLeave: label = "Leave Game"; break;
    case kLichessAnalyse:
        label = lichess.onlineSession && !lichess.inGame && !lichess.gameId.empty()
                    ? "Open Game on Lichess"
                    : "Analyse Game on Lichess";
        break;
    case kLichessPlayAi: label = "Play Lichess AI"; break;
    case kLichessChallenge:
        label = lichess.challenging ? "Cancel Challenge" : "Challenge Player...";
        break;
    case kLichessSeek: label = lichess.seeking ? "Cancel Search" : "Find Opponent"; break;
    case kLichessTime:
        label = std::string("Time: ") + lichessTimeControls()[lichess.timeControl].label;
        break;
    case kLichessAnimations:
        label = std::string("Animations: ") + lichessPolicyName(lichess.policy);
        break;
    case kLichessDraw: label = lichess.opponentOffersDraw ? "Accept Draw" : "Offer Draw"; break;
    case kLichessResign:
        label = splitUciMoves(lichess.state.moves).size() < 2 ? "Abort Game" : "Resign";
        break;
    default: label = "-";
    }
    return label.c_str();
}

bool Application::lichessItemEnabled(int item) const {
    const bool idle =
        lichess.connected && !lichess.inGame && !lichess.seeking && !lichess.challenging;
    switch (item) {
    case kLichessConnect: return !lichess.connecting;
    case kLichessForget: return !lichess.inGame;
    case kLichessGames: return lichess.connected;
    case kLichessLeave: return lichess.inGame;
    case kLichessAnalyse:
        return !lichess.inGame && ((lichess.onlineSession && !lichess.gameId.empty()) ||
                                   !session.past.empty());
    case kLichessPlayAi: return idle;
    case kLichessChallenge: return idle || (lichess.challenging && !lichess.inGame);
    case kLichessSeek:
        return lichess.connected && !lichess.inGame && !lichess.challenging &&
               (lichess.seeking || lichessTimeControls()[lichess.timeControl].seekable);
    case kLichessTime: return !lichess.inGame && !lichess.seeking;
    case kLichessAnimations: return true;
    case kLichessDraw:
    case kLichessResign: return lichess.inGame;
    default: return false;
    }
}

std::string Application::lichessColorPreference() const {
    bool whiteHuman = settings.white_player == 0, blackHuman = settings.black_player == 0;
    if (whiteHuman && !blackHuman)
        return "white";
    if (blackHuman && !whiteHuman)
        return "black";
    return "random";
}

void Application::lichessMenuAction(int item) {
    switch (item) {
    case kLichessConnect: {
        if (lichess.connected || lichess.connecting) {
            lichessDisconnect();
            return;
        }
        const char *environment = std::getenv("BC_LICHESS_TOKEN");
        std::string token = environment && *environment ? environment : "";
        if (token.empty() && !lichess.offline)
            token = loadLichessToken();
        if (!token.empty()) {
            lichessConnect(token, false);
            return;
        }
        openLichessPrompt(kTokenPrompt);
        openUrl(lichess, kTokenPage);
        return;
    }
    case kLichessForget:
        if (!lichess.offline)
            forgetLichessToken();
        lichessDisconnect();
        lichess.token.clear();
        showAlert(lichessTokenStorageAvailable() ? "The Lichess token was removed from the Keychain."
                                                 : "The Lichess token was forgotten.");
        return;
    case kLichessAnalyse:
        if (lichess.onlineSession && !lichess.gameId.empty())
            openUrl(lichess, baseUrl() + "/" + lichess.gameId);
        else
            lichessAnalyse();
        return;
    case kLichessGames: openLichessLobby(); return;
    case kLichessLeave: lichessLeaveGame(); return;
    case kLichessPlayAi: lichessStartGame("ai"); return;
    case kLichessChallenge:
        if (lichess.challenging) {
            // Closing the keep-alive stream withdraws the challenge.
            lichessCloseStream("challenge");
            lichess.challenging = lichess.expectingGame = false;
        } else {
            openLichessPrompt(kUsernamePrompt);
        }
        return;
    case kLichessSeek:
        if (lichess.seeking) {
            lichessCloseStream("seek");
            lichess.seeking = lichess.expectingGame = false;
        } else {
            lichessStartGame("seek");
        }
        return;
    case kLichessTime:
        lichess.timeControl = (lichess.timeControl + 1) % lichessTimeControls().size();
        return;
    case kLichessAnimations:
        lichess.policy = LichessAnimationPolicy((int(lichess.policy) + 1) % 4);
        return;
    case kLichessDraw:
        // "yes" both offers and accepts.
        lichessRequest("draw", "POST", "/api/board/game/" + lichess.gameId + "/draw/yes");
        return;
    case kLichessResign: {
        bool abort = splitUciMoves(lichess.state.moves).size() < 2;
        dialogButton.cancel();
        alertId = 402;
        message = abort ? "Abort this Lichess game?" : "Resign this Lichess game?";
        confirmAction = kConfirmResign;
        return;
    }
    default: return;
    }
}

// ---------------------------------------------------------------- connection

void Application::lichessConnect(const std::string &token, bool fromPrompt) {
    lichess.token = token;
    lichess.tokenFromPrompt = fromPrompt;
    lichess.connecting = true;
    lichessRequest("account", "GET", "/api/account");
}

void Application::lichessDisconnect() {
    lichessCloseStream("events");
    lichessCloseStream("game");
    lichessCloseStream("seek");
    lichessCloseStream("challenge");
    lichess.connected = lichess.connecting = lichess.seeking = false;
    lichess.inGame = false;
    lichess.reconnectEventsAt = lichess.reconnectGameAt = 0;
    lichess.challenges.clear();
    lichess.promptedChallenge.clear();
    lichess.games.clear();
    lichess.lobbyOpen = lichess.lobbyOnConnect = false;
    lichess.lobbyStatus.clear();
    lichess.expectingGame = lichess.challenging = false;
}

/* Upload the session's PGN for analysis; works without a token. */
void Application::lichessAnalyse() {
    std::string pgn = exportPgn(session.past, session.game, PgnTags{});
    if (pgn.empty()) {
        showAlert("This game cannot be exported as PGN.");
        return;
    }
    lichessRequest("import", "POST", "/api/import", "pgn=" + formEscape(pgn));
}

/* Lichess AI, a direct challenge or a public seek, with the menu's time control.
 * Colour follows Settings: a single human side plays that colour, else random. */
void Application::lichessStartGame(const std::string &kind, const std::string &username) {
    const auto &control = lichessTimeControls()[lichess.timeControl];
    std::string color = "color=" + lichessColorPreference();
    lichess.expectingGame = true;
    std::string clock = control.days ? "days=" + std::to_string(control.days)
                                     : "clock.limit=" + std::to_string(control.minutes * 60) +
                                           "&clock.increment=" + std::to_string(control.increment);
    if (kind == "ai") {
        // Novice and Level 1 map to AI level 1; Levels 8 and 9 (and custom time) cap at 8.
        int level = settings.level <= 1 ? 1 : settings.level >= 8 ? 8 : settings.level;
        lichessRequest("challenge-ai", "POST", "/api/challenge/ai",
                       "level=" + std::to_string(level) + "&" + clock + "&" + color);
    } else if (kind == "challenge") {
        // keepAliveStream keeps the challenge open until it is answered.
        lichessOpenStream("challenge", "POST", "/api/challenge/" + username,
                          "rated=false&" + clock + "&" + color + "&keepAliveStream=true");
        lichess.challenging = true;
        showAlert("Challenge sent to " + username +
                  ". Lichess > Cancel Challenge withdraws it.");
    } else {
        std::string seek = control.days ? "rated=false&days=" + std::to_string(control.days)
                                        : "rated=false&time=" + std::to_string(control.minutes) +
                                              "&increment=" + std::to_string(control.increment);
        lichessOpenStream("seek", "POST", "/api/board/seek", seek + "&" + color);
        lichess.seeking = true;
    }
}

// ---------------------------------------------------------------- incoming

void Application::updateLichess() {
#if BC_HAVE_LICHESS
    if (lichess.client)
        for (const auto &message : lichess.client->poll())
            handleLichessMessage(message);
#endif
    Uint64 now = SDL_GetTicks();
    if (lichess.connected && lichess.reconnectEventsAt && now >= lichess.reconnectEventsAt) {
        lichess.reconnectEventsAt = 0;
        lichessOpenStream("events", "GET", "/api/stream/event");
    }
    if (lichess.inGame && lichess.reconnectGameAt && now >= lichess.reconnectGameAt) {
        lichess.reconnectGameAt = 0;
        lichessOpenStream("game", "GET", "/api/board/game/stream/" + lichess.gameId);
    }
    if (!animation.busy() && !session.pending)
        animation.minimumSpeed = 1;
}

void Application::handleLichessMessage(const LichessMessage &incoming) {
    using Kind = LichessMessage::Kind;
    const std::string &tag = incoming.tag;
    if (incoming.kind == Kind::line) {
        auto json = Json::parse(incoming.body);
        if (!json)
            return; // Not JSON: ignore rather than guess.
        if (tag == "events")
            handleLichessEvent(*json);
        else if (tag == "game")
            handleLichessGameLine(*json);
        else if (tag == "challenge") {
            std::string done = (*json)["done"].str();
            if (!done.empty())
                lichess.challenging = false;
            if (done == "declined" || done == "canceled" || done == "expired")
                lichess.expectingGame = false;
            if (done == "declined")
                showAlert("The challenge was declined.");
            else if (done == "canceled" || done == "expired")
                showAlert("The challenge was not answered.");
        }
        return;
    }
    if (incoming.kind == Kind::closed) {
        if (tag == "events" && lichess.connected) {
            if (incoming.status == 401) {
                lichessDisconnect();
                showAlert("Lichess no longer accepts this token.");
            } else {
                lichess.reconnectEventsAt = SDL_GetTicks() + kEventsRetry;
            }
        } else if (tag == "game" && lichess.inGame) {
            if (incoming.status >= 400 && incoming.status < 500) {
                // Refused (for example a game the Board API cannot play): leave it.
                lichess.inGame = lichess.onlineSession = false;
                for (auto &listed : lichess.games)
                    if (listed.id == lichess.gameId)
                        listed.boardPlayable = false;
                showAlert("Lichess will not stream this game here (" + errorText(incoming) +
                          "). Play it on lichess.org.");
            } else {
                lichess.reconnectGameAt = SDL_GetTicks() + kGameRetry;
            }
        } else if (tag == "seek") {
            lichess.seeking = false;
            if (incoming.status != 200)
                showAlert("Lichess did not accept the search: " + errorText(incoming));
        } else if (tag == "challenge") {
            lichess.challenging = false;
            if (incoming.status != 200 && incoming.status != 0) {
                lichess.expectingGame = false;
                showAlert("Lichess did not accept the challenge: " + errorText(incoming));
            }
        }
        return;
    }
    // Responses to ordinary requests.
    const bool ok = incoming.status >= 200 && incoming.status < 300;
    if (incoming.status == 429) {
        showAlert("Lichess asks to slow down. Try again in a minute.");
        lichess.connecting = false;
        return;
    }
    if (tag == "account") {
        lichess.connecting = false;
        if (ok) {
            auto json = Json::parse(incoming.body);
            lichess.username = json ? (*json)["username"].str("Lichess player") : "Lichess player";
            lichess.connected = true;
            if (lichess.tokenFromPrompt && !lichess.offline && !saveLichessToken(lichess.token) &&
                lichessTokenStorageAvailable())
                showAlert("Connected, but the token could not be saved in the Keychain.");
            lichessOpenStream("events", "GET", "/api/stream/event");
            // Running games show up in the lobby instead of taking over the board.
            lichess.lobbyOnConnect = true;
            lichessRequest("playing", "GET", "/api/account/playing?nb=50");
        } else if (incoming.status == 401) {
            if (!lichess.tokenFromPrompt && !lichess.offline && !std::getenv("BC_LICHESS_TOKEN"))
                forgetLichessToken();
            lichess.token.clear();
            showAlert("Lichess did not accept this token.");
        } else {
            showAlert("Unable to reach Lichess: " + errorText(incoming));
        }
        return;
    }
    if (tag == "playing") {
        auto json = ok ? Json::parse(incoming.body) : std::nullopt;
        if (!json)
            return;
        std::vector<LichessState::LobbyGame> games;
        for (const auto &item : (*json)["nowPlaying"].items()) {
            LichessState::LobbyGame game;
            game.id = item["gameId"].str();
            game.color = item["color"].str("white");
            const Json &opponent = item["opponent"];
            game.opponent = opponent["ai"].integer()
                                ? "Lichess AI level " + std::to_string(opponent["ai"].integer())
                                : opponent["username"].str("Opponent");
            if (long long rating = opponent["rating"].integer())
                game.opponent += " (" + std::to_string(rating) + ")";
            game.speed = item["speed"].str();
            game.myTurn = item["isMyTurn"].boolean();
            game.secondsLeft = item["secondsLeft"].integer(-1);
            // The Board API cannot play bullet; those games stay on lichess.org.
            game.boardPlayable = game.speed != "bullet" && game.speed != "ultraBullet";
            for (const auto &known : lichess.games)
                if (known.id == game.id && !known.boardPlayable)
                    game.boardPlayable = false;
            if (!game.id.empty())
                games.push_back(game);
        }
        lichess.games = games;
        if (lichess.lobbyOnConnect) {
            lichess.lobbyOnConnect = false;
            if (!lichess.games.empty() && !lichess.inGame) {
                lichess.lobbyOpen = true;
                lichess.lobbySelection = 0;
            }
        }
        return;
    }
    if (tag == "import") {
        auto json = ok ? Json::parse(incoming.body) : std::nullopt;
        std::string url = json ? (*json)["url"].str() : "";
        if (!url.empty())
            openUrl(lichess, url);
        else
            showAlert("Lichess did not import the game: " + errorText(incoming));
        return;
    }
    if (!ok) {
        if (tag == "move") {
            // The server rejected a move: rebuild from its state rather than guess.
            lichessCloseStream("game");
            lichessOpenStream("game", "GET", "/api/board/game/stream/" + lichess.gameId);
        }
        showAlert("Lichess: " + errorText(incoming));
    }
}

void Application::handleLichessEvent(const Json &event) {
    const std::string type = event["type"].str();
    if (type == "gameStart") {
        const Json &game = event["game"];
        std::string id = game["gameId"].str();
        if (id.empty() || (lichess.inGame && id == lichess.gameId))
            return;
        const Json &opponent = game["opponent"];
        std::string name = opponent["ai"].integer()
                               ? "Lichess AI level " + std::to_string(opponent["ai"].integer())
                               : opponent["username"].str("Opponent");
        std::string color = game["color"].str("white");
        // Games the Board API cannot play (for example bullet from the website) stay there.
        bool playable = !game["compat"].has("board") || game["compat"]["board"].boolean();
        if (!playable) {
            for (auto &listed : lichess.games)
                if (listed.id == id)
                    listed.boardPlayable = false;
            bool known = false;
            for (const auto &listed : lichess.games)
                known |= listed.id == id;
            if (!known) {
                LichessState::LobbyGame listed;
                listed.id = id;
                listed.color = color;
                listed.opponent = name;
                listed.speed = game["speed"].str();
                listed.boardPlayable = false;
                lichess.games.push_back(listed);
            }
            if (lichess.expectingGame) {
                lichess.expectingGame = false;
                showAlert("This Lichess game cannot be played from here. Play it on lichess.org.");
            }
            return;
        }
        if (lichess.expectingGame && !lichess.inGame) {
            lichessJoinGame(id, color, name);
            return;
        }
        bool known = false;
        for (auto &listed : lichess.games)
            known |= listed.id == id;
        if (!known) {
            LichessState::LobbyGame listed;
            listed.id = id;
            listed.color = color;
            listed.opponent = name;
            listed.speed = game["speed"].str();
            listed.myTurn = game["isMyTurn"].boolean();
            listed.secondsLeft = game["secondsLeft"].integer(-1);
            lichess.games.push_back(listed);
        }
        if (!lichess.inGame && !lichess.lobbyOpen) {
            lichess.lobbyOpen = true;
            lichess.lobbySelection = 0;
        }
    } else if (type == "gameFinish") {
        std::string id = event["game"]["gameId"].str();
        for (auto it = lichess.games.begin(); it != lichess.games.end(); ++it)
            if (it->id == id) {
                lichess.games.erase(it);
                break;
            }
    } else if (type == "challenge") {
        LichessChallenge challenge = parseChallenge(event["challenge"]);
        if (!challenge.incoming || challenge.id.empty())
            return;
        for (const auto &queued : lichess.challenges)
            if (queued.id == challenge.id)
                return;
        lichess.challenges.push_back(challenge);
        // Show it in the lobby, selected, unless a game is being played.
        if (!lichess.inGame) {
            lichess.lobbyOpen = true;
            auto rows = lobbyRows();
            for (size_t i = 0; i < rows.size(); ++i)
                if (rows[i].challenge && lichess.challenges[rows[i].index].id == challenge.id)
                    lichess.lobbySelection = int(i);
        }
    } else if (type == "challengeCanceled") {
        std::string id = event["challenge"]["id"].str();
        for (auto it = lichess.challenges.begin(); it != lichess.challenges.end(); ++it)
            if (it->id == id) {
                lichess.challenges.erase(it);
                break;
            }

    }
}

void Application::lichessJoinGame(const std::string &id, const std::string &color,
                                  const std::string &opponent) {
    lichess.gameId = id;
    lichess.color = color;
    lichess.opponent = opponent;
    lichess.inGame = lichess.onlineSession = true;
    lichess.expectingGame = lichess.challenging = false;
    // The 2D board turns to the player's side (the 3D artwork stays White's view).
    rotateFlat = color == "black";
    lichess.opponentOffersDraw = lichess.drawPrompted = lichess.opponentGone = false;
    lichess.meta = LichessGameFull{};
    lichess.state = LichessGameState{};
    if (lichess.seeking) {
        lichessCloseStream("seek");
        lichess.seeking = false;
    }
    lichessCloseStream("challenge");
    lichess.lobbyOpen = false;
    lichess.lobbyStatus.clear();
    // Leave local-only modes; the recovered engine stays silent from here on.
    computer.cancel();
    hint.reset();
    computerEnding = 0;
    if (editing) {
        editing = false;
        flat = previousFlat;
    }
    textDialog.open = false;
    lichessOpenStream("game", "GET", "/api/board/game/stream/" + id);
}

void Application::handleLichessGameLine(const Json &event) {
    const std::string type = event["type"].str();
    if (type == "gameFull") {
        lichess.meta = parseGameFull(event);
        lichess.state = lichess.meta.state;
        lichess.speed = lichess.meta.speed;
        const auto &other = lichess.color == "white" ? lichess.meta.black : lichess.meta.white;
        lichess.opponent = other.name;
        lichess.stateAt = SDL_GetTicks();
        syncLichessGame(true);
    } else if (type == "gameState") {
        lichess.state = parseGameState(event);
        lichess.stateAt = SDL_GetTicks();
        syncLichessGame(false);
    } else if (type == "opponentGone") {
        lichess.opponentGone = event["gone"].boolean();
    }
}

/* Bring the session to the server's move list. A single new move animates (if it is
 * the opponent's and the policy allows); anything else rebuilds without animation. */
void Application::syncLichessGame(bool full) {
    LichessLine line = buildLichessLine(lichess.meta.initialFen, lichess.state.moves);
    if (!line.error.empty()) {
        if (full) {
            showAlert(line.error);
        } else {
            lichessCloseStream("game");
            lichessOpenStream("game", "GET", "/api/board/game/stream/" + lichess.gameId);
        }
        return;
    }
    std::vector<BCGame> games{line.start};
    for (const auto &move : line.moves) {
        BCGame next = games.back();
        bcGameApply(&next, move, nullptr);
        games.push_back(next);
    }
    const size_t have = session.past.size(), total = line.moves.size();
    bool consistent = !full && have <= total &&
                      std::memcmp(&games[have].position, &session.game.position,
                                  sizeof(Position)) == 0;
    if (consistent && have + 1 == total) {
        const BCMove move = line.moves.back();
        const bool ownMove = (games[have].position.side == 0) == (lichess.color == "white");
        auto style = ownMove ? LichessMoveAnimation::none
                                 : chooseLichessAnimation(lichess.policy, lichess.speed,
                                                          lichess.meta.clockInitial > 0,
                                                          lichessClock(lichess.color == "white"),
                                                          isCaptureIn(games[have], move));
        if (session.promotionWalk || !session.promotion.empty()) {
            session.promotion.clear();
            session.promotionWalk = false;
            session.pending.reset();
        }
        if (style == LichessMoveAnimation::none) {
            session.apply(move, false);
        } else {
            // A new move replaces any presentation still waiting for its turn.
            if (session.pending && !session.pendingSent)
                sendMove(*session.pending, session.animationBefore.side);
            session.apply(move, true);
            session.pendingSent = true; // Opponent moves are never sent back.
            lichess.fastPending = style == LichessMoveAnimation::fast;
        }
    } else if (!consistent || have != total) {
        animation.cancel();
        session = GameSession();
        session.game = line.start;
        for (const auto &move : line.moves)
            session.apply(move, false);
    }
    session.bookEligible = false;
    session.computerRequested = false;

    bool offer = lichess.color == "white" ? lichess.state.bdraw : lichess.state.wdraw;
    lichess.opponentOffersDraw = offer;
    if (!offer)
        lichess.drawPrompted = false;
    if (lichessGameOver(lichess.state.status)) {
        if (lichess.inGame) {
            lichess.inGame = false;
            lichessCloseStream("game");
            session.endingAnnounced = true;
            showAlert(lichessEndMessage(lichess.state, lichess.color, lichess.opponent));
        }
        return;
    }
    if (offer && !lichess.drawPrompted && !alertId) {
        lichess.drawPrompted = true;
        dialogButton.cancel();
        alertId = 402;
        message = lichess.opponent + " offers a draw. Accept?";
        confirmAction = kConfirmAcceptDraw;
    }
}

void Application::lichessDismissAlert(int command, int item) {
    if (command == kConfirmAcceptChallenge) {
        std::string id = lichess.promptedChallenge;
        lichess.promptedChallenge.clear();
        if (!lichess.challenges.empty() && lichess.challenges.front().id == id)
            lichess.challenges.pop_front();
        if (id.empty())
            return;
        if (item == 1) {
            lichess.expectingGame = true;
            lichessRequest("accept", "POST", "/api/challenge/" + id + "/accept");
        }
        else
            lichessRequest("decline", "POST", "/api/challenge/" + id + "/decline",
                           "reason=generic");
    } else if (command == kConfirmAcceptDraw && lichess.inGame) {
        lichessRequest("draw", "POST",
                       "/api/board/game/" + lichess.gameId + "/draw/" + (item == 1 ? "yes" : "no"));
    } else if (command == kConfirmResign && item == 1 && lichess.inGame) {
        bool abort = splitUciMoves(lichess.state.moves).size() < 2;
        lichessRequest(abort ? "abort" : "resign", "POST",
                       "/api/board/game/" + lichess.gameId + (abort ? "/abort" : "/resign"));
    }
}

// ---------------------------------------------------------------- play

bool Application::lichessOwnTurn() const {
    return lichess.inGame && (session.game.position.side == 0) == (lichess.color == "white");
}

/* Remaining time from the last server state, counting down for the side to move once
 * both players have moved (Lichess starts the clocks then). */
long long Application::lichessClock(bool white) const {
    long long millis = white ? lichess.state.wtime : lichess.state.btime;
    bool running = lichess.inGame && lichess.state.status == "started" &&
                   splitUciMoves(lichess.state.moves).size() >= 2 &&
                   (session.game.position.side == 0) == white;
    if (running)
        millis -= (long long)(SDL_GetTicks() - lichess.stateAt);
    return millis < 0 ? 0 : millis;
}

/* A click during an online animation finishes it at once: the clock is the player's. */
void Application::finishLichessAnimation() {
    if (session.promotionWalk)
        return;
    if (session.pending && !session.pendingSent)
        sendMove(*session.pending, session.animationBefore.side);
    animation.cancel();
    session.pending.reset();
    session.pendingSent = false;
    lichess.fastPending = false;
    animation.minimumSpeed = 1;
}

void Application::renderLichessClocks() {
    const int textY = chicago_available ? 1 : 6;
    if (lichess.inGame && lichess.meta.clockInitial <= 0) {
        std::string text = lichessOwnTurn() ? "Your move" : "Waiting for opponent";
        setGrayscale(rendererPtr, 0);
        drawSystemText(rendererPtr, viewWidth() - 8 - systemTextWidth(text), textY, text.c_str());
        return;
    }
    if (!lichess.onlineSession || lichess.meta.clockInitial <= 0)
        return;
    int right = viewWidth() - 8;
    for (bool white : {false, true}) {
        std::string text = std::string(white ? "White " : "Black ") +
                           formatLichessClock(lichessClock(white));
        int width = systemTextWidth(text);
        int left = right - width;
        bool active = lichess.inGame && (session.game.position.side == 0) == white;
        if (active) {
            setGrayscale(rendererPtr, 0);
            drawRectangle(rendererPtr, left - 3, 1, width + 6, 17);
        }
        setGrayscale(rendererPtr, active ? 255 : 0);
        drawSystemText(rendererPtr, left, textY, text.c_str());
        right = left - 16;
    }
}

// ---------------------------------------------------------------- text prompts

namespace {
    constexpr int kPromptWidth = 400, kPromptHeight = 112, kButtonWidth = 70, kButtonHeight = 20;

    SDL_Rect promptBox(int viewWidth) {
        return {(viewWidth - kPromptWidth) / 2, 20 + (320 - kPromptHeight) / 2, kPromptWidth,
                kPromptHeight};
    }
    SDL_Rect okButton(SDL_Rect box) {
        return {box.x + box.w - kButtonWidth - 14, box.y + box.h - kButtonHeight - 12,
                kButtonWidth, kButtonHeight};
    }
    SDL_Rect cancelButton(SDL_Rect box) {
        SDL_Rect ok = okButton(box);
        return {ok.x - kButtonWidth - 12, ok.y, kButtonWidth, kButtonHeight};
    }
    bool inside(SDL_Rect r, int x, int y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }
}

void Application::openLichessPrompt(int kind) {
    dialogButton.cancel();
    textDialog.resourceId = kind;
    textDialog.text.clear();
    textDialog.cursor = textDialog.anchor = 0;
    textDialog.open = true;
    require(SDL_StartTextInput(window), "Start Lichess input");
}

void Application::renderLichessPrompt() {
    SDL_Rect box = promptBox(viewWidth());
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, box.x, box.y, box.w, box.h);
    setGrayscale(rendererPtr, 255);
    drawRectangle(rendererPtr, box.x + 1, box.y + 1, box.w - 2, box.h - 2);
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, box.x + 3, box.y + 3, box.w - 6, box.h - 6, false);
    drawRectangle(rendererPtr, box.x + 4, box.y + 4, box.w - 8, box.h - 8, false);
    const int offset = chicago_available ? 0 : 4;
    bool token = textDialog.resourceId == kTokenPrompt;
    drawSystemText(rendererPtr, box.x + 14, box.y + 10 + offset,
                   token ? "Paste a Lichess API token (Command/Ctrl+V)." : "Challenge which Lichess player?");
    drawSystemText(rendererPtr, box.x + 14, box.y + 28 + offset,
                   token ? "Scopes: board:play, challenge:read, challenge:write."
                         : "Uses the Time setting from the Lichess menu.");
    SDL_Rect field{box.x + 14, box.y + 50, box.w - 28, 20};
    drawRectangle(rendererPtr, field.x, field.y, field.w, field.h, false);
    // Tokens show as asterisks; long text shows its end.
    std::string shown = token ? std::string(textDialog.text.size(), '*') : textDialog.text;
    while (!shown.empty() && systemTextWidth(shown) > field.w - 10)
        shown.erase(0, 1);
    drawSystemText(rendererPtr, field.x + 4, field.y + 2 + offset, shown.c_str());
    drawRectangle(rendererPtr, field.x + 4 + systemTextWidth(shown), field.y + 3, 1, 14);
    for (auto [rect, label] : {std::pair{okButton(box), "OK"}, std::pair{cancelButton(box), "Cancel"}}) {
        drawRectangle(rendererPtr, rect.x, rect.y, rect.w, rect.h, false);
        if (std::strcmp(label, "OK") == 0)
            drawRectangle(rendererPtr, rect.x - 3, rect.y - 3, rect.w + 6, rect.h + 6, false);
        drawSystemText(rendererPtr, rect.x + (rect.w - systemTextWidth(label)) / 2,
                       rect.y + 2 + offset, label);
    }
}

bool Application::lichessPromptPress(int x, int y) {
    SDL_Rect box = promptBox(viewWidth());
    return inside(okButton(box), x, y) || inside(cancelButton(box), x, y);
}

void Application::lichessPromptRelease(int x, int y) {
    SDL_Rect box = promptBox(viewWidth());
    if (inside(okButton(box), x, y)) {
        acceptLichessPrompt();
    } else if (inside(cancelButton(box), x, y)) {
        textDialog.open = false;
        require(SDL_StopTextInput(window), "Stop Lichess input");
    }
}

void Application::acceptLichessPrompt() {
    std::string text = textDialog.text;
    int kind = textDialog.resourceId;
    textDialog.open = false;
    require(SDL_StopTextInput(window), "Stop Lichess input");
    if (text.empty())
        return;
    if (kind == kTokenPrompt)
        lichessConnect(text, true);
    else
        lichessStartGame("challenge", text);
}

/* Stop following the online game without resigning: it continues on Lichess (with the
 * player's clock running) and can be reopened from the lobby. */
void Application::lichessLeaveGame() {
    if (!lichess.inGame)
        return;
    lichessCloseStream("game");
    finishLichessAnimation();
    lichess.inGame = lichess.onlineSession = false;
    lichess.reconnectGameAt = 0;
    session.computerRequested = false;
    showAlert("The game continues on Lichess and your clock keeps running. Reopen it with "
              "Lichess > Games...");
}

// ---------------------------------------------------------------- lobby

namespace {
    constexpr int kLobbyWidth = 480, kLobbyHeight = 276, kLobbyRows = 7, kRowHeight = 18;
    constexpr Uint64 kDoubleClick = 400;
}

std::vector<Application::LobbyRow> Application::lobbyRows() const {
    std::vector<LobbyRow> rows;
    for (size_t i = 0; i < lichess.challenges.size(); ++i)
        rows.push_back({true, i});
    for (size_t i = 0; i < lichess.games.size(); ++i)
        rows.push_back({false, i});
    return rows;
}

std::string Application::lobbyRowText(const LobbyRow &row) const {
    if (row.challenge) {
        const auto &c = lichess.challenges[row.index];
        return "Challenge from " + c.challenger + ": " + c.timeControl +
               (c.rated ? " rated" : " casual");
    }
    const auto &g = lichess.games[row.index];
    std::string text = (lichess.inGame && g.id == lichess.gameId ? "> " : "") + g.opponent +
                       " - " + g.speed + " - " + (g.myTurn ? "your move" : "their move");
    if (g.secondsLeft >= 0 && g.speed != "correspondence")
        text += " - " + formatLichessClock(g.secondsLeft * 1000);
    if (!g.boardPlayable)
        text += " - on lichess.org";
    return text;
}

SDL_Rect Application::lobbyBox() const {
    return {(viewWidth() - kLobbyWidth) / 2, 20 + (original_content_height - kLobbyHeight) / 2,
            kLobbyWidth, kLobbyHeight};
}

SDL_Rect Application::lobbyButtonRect(int button) const {
    SDL_Rect box = lobbyBox();
    const int upper = box.y + 192, lower = box.y + box.h - 34;
    switch (button) {
    case kLobbyOpen: return {box.x + box.w - 14 - 90, upper, 90, 20};
    case kLobbyDecline: return {box.x + box.w - 14 - 90 - 12 - 80, upper, 80, 20};
    case kLobbyTime: return {box.x + 14, upper, 170, 20};
    case kLobbyPlayAi: return {box.x + 14, lower, 80, 20};
    case kLobbyChallenge: return {box.x + 14 + 92, lower, 100, 20};
    case kLobbySeek: return {box.x + 14 + 204, lower, 110, 20};
    default: return {box.x + box.w - 14 - 70, lower, 70, 20};
    }
}

const char *Application::lobbyButtonLabel(int button) const {
    static std::string labels[kLobbyButtonCount];
    std::string &label = labels[button];
    auto rows = lobbyRows();
    const LobbyRow *row = lichess.lobbySelection >= 0 && size_t(lichess.lobbySelection) < rows.size()
                              ? &rows[size_t(lichess.lobbySelection)]
                              : nullptr;
    switch (button) {
    case kLobbyOpen:
        label = row && row->challenge                                   ? "Accept"
                : row && !lichess.games[row->index].boardPlayable       ? "In Browser"
                                                                        : "Open";
        break;
    case kLobbyDecline: label = "Decline"; break;
    case kLobbyTime:
        label = std::string("Time: ") + lichessTimeControls()[lichess.timeControl].label;
        break;
    case kLobbyPlayAi: label = "Play AI"; break;
    case kLobbyChallenge: label = lichess.challenging ? "Cancel Ch." : "Challenge..."; break;
    case kLobbySeek: label = lichess.seeking ? "Cancel Search" : "Find Opponent"; break;
    default: label = "Close";
    }
    return label.c_str();
}

bool Application::lobbyButtonEnabled(int button) const {
    auto rows = lobbyRows();
    bool selected = lichess.lobbySelection >= 0 && size_t(lichess.lobbySelection) < rows.size();
    switch (button) {
    case kLobbyOpen: return selected;
    case kLobbyDecline: return selected && rows[size_t(lichess.lobbySelection)].challenge;
    case kLobbyTime: return lichessItemEnabled(kLichessTime);
    case kLobbyPlayAi: return lichessItemEnabled(kLichessPlayAi);
    case kLobbyChallenge: return lichessItemEnabled(kLichessChallenge);
    case kLobbySeek: return lichessItemEnabled(kLichessSeek);
    default: return true;
    }
}

void Application::openLichessLobby() {
    if (!lichess.lobbyOpen) {
        lichess.lobbyOpen = true;
        lichess.lobbySelection = 0;
        lichess.lobbyStatus.clear();
    }
    lichessRequest("playing", "GET", "/api/account/playing?nb=50");
}

void Application::renderLichessLobby() {
    SDL_Rect box = lobbyBox();
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, box.x, box.y, box.w, box.h);
    setGrayscale(rendererPtr, 255);
    drawRectangle(rendererPtr, box.x + 1, box.y + 1, box.w - 2, box.h - 2);
    setGrayscale(rendererPtr, 0);
    drawRectangle(rendererPtr, box.x + 3, box.y + 3, box.w - 6, box.h - 6, false);
    drawRectangle(rendererPtr, box.x + 4, box.y + 4, box.w - 8, box.h - 8, false);
    const int offset = chicago_available ? 0 : 4;
    std::string title = "Lichess games" +
                        (lichess.username.empty() ? std::string() : " - " + lichess.username);
    drawSystemText(rendererPtr, box.x + 14, box.y + 10 + offset, title.c_str());

    // The list: challenges first, then games (most urgent first, as Lichess sends them).
    SDL_Rect list{box.x + 14, box.y + 32, box.w - 28, kLobbyRows * kRowHeight + 4};
    drawRectangle(rendererPtr, list.x, list.y, list.w, list.h, false);
    auto rows = lobbyRows();
    if (rows.empty())
        drawSystemText(rendererPtr, list.x + 6, list.y + 2 + offset,
                       "No games in progress. Start one below.");
    int first = std::max(0, lichess.lobbySelection - (kLobbyRows - 1));
    for (int i = first; i < int(rows.size()) && i < first + kLobbyRows; ++i) {
        int y = list.y + 2 + (i - first) * kRowHeight;
        bool selected = i == lichess.lobbySelection;
        if (selected) {
            setGrayscale(rendererPtr, 0);
            drawRectangle(rendererPtr, list.x + 2, y, list.w - 4, kRowHeight);
        }
        setGrayscale(rendererPtr, selected ? 255 : 0);
        std::string text = lobbyRowText(rows[size_t(i)]);
        while (!text.empty() && systemTextWidth(text) > list.w - 12)
            text.pop_back();
        drawSystemText(rendererPtr, list.x + 6, y + offset, text.c_str());
    }
    setGrayscale(rendererPtr, 0);
    std::string status = !lichess.lobbyStatus.empty() ? lichess.lobbyStatus
                         : lichess.seeking            ? "Searching for an opponent..."
                         : lichess.challenging        ? "Waiting for your challenge to be answered..."
                         : lichess.inGame ? "Leave or finish the current game to start another."
                                          : "";
    drawSystemText(rendererPtr, box.x + 14, box.y + 170 + offset, status.c_str());

    for (int button = 0; button < kLobbyButtonCount; ++button) {
        SDL_Rect r = lobbyButtonRect(button);
        bool enabled = lobbyButtonEnabled(button);
        bool pressed = lichess.lobbyPressed == button;
        setGrayscale(rendererPtr, 0);
        if (pressed)
            drawRectangle(rendererPtr, r.x, r.y, r.w, r.h);
        else
            drawRectangle(rendererPtr, r.x, r.y, r.w, r.h, false);
        if (button == kLobbyOpen && enabled)
            drawRectangle(rendererPtr, r.x - 3, r.y - 3, r.w + 6, r.h + 6, false);
        const char *label = lobbyButtonLabel(button);
        setGrayscale(rendererPtr, pressed ? 255 : enabled ? 0 : 150);
        drawSystemText(rendererPtr, r.x + (r.w - systemTextWidth(label)) / 2, r.y + 2 + offset,
                       label);
    }
}

void Application::lichessLobbyPress(int x, int y) {
    auto inside = [](SDL_Rect r, int px, int py) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    SDL_Rect box = lobbyBox();
    SDL_Rect list{box.x + 14, box.y + 32, box.w - 28, kLobbyRows * kRowHeight + 4};
    if (inside(list, x, y)) {
        auto rows = lobbyRows();
        int first = std::max(0, lichess.lobbySelection - (kLobbyRows - 1));
        int row = first + (y - list.y - 2) / kRowHeight;
        if (row < 0 || row >= int(rows.size()))
            return;
        Uint64 now = SDL_GetTicks();
        bool twice = row == lichess.lobbyClickRow && now - lichess.lobbyClickAt < kDoubleClick;
        lichess.lobbySelection = row;
        lichess.lobbyClickRow = row;
        lichess.lobbyClickAt = now;
        if (twice) {
            lichess.lobbyClickRow = -1;
            lichessLobbyAction(kLobbyOpen);
        }
        return;
    }
    for (int button = 0; button < kLobbyButtonCount; ++button)
        if (inside(lobbyButtonRect(button), x, y) && lobbyButtonEnabled(button))
            lichess.lobbyPressed = button;
}

void Application::lichessLobbyRelease(int x, int y) {
    int button = lichess.lobbyPressed;
    lichess.lobbyPressed = -1;
    SDL_Rect r = button >= 0 ? lobbyButtonRect(button) : SDL_Rect{};
    if (button >= 0 && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h)
        lichessLobbyAction(button);
}

void Application::lichessLobbyKey(SDL_Keycode key) {
    int count = int(lobbyRows().size());
    if (key == SDLK_UP && lichess.lobbySelection > 0)
        --lichess.lobbySelection;
    else if (key == SDLK_DOWN && lichess.lobbySelection + 1 < count)
        ++lichess.lobbySelection;
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
        lichessLobbyAction(kLobbyOpen);
    else if (key == SDLK_ESCAPE)
        lichessLobbyAction(kLobbyClose);
}

void Application::lichessLobbyAction(int button) {
    if (!lobbyButtonEnabled(button))
        return;
    auto rows = lobbyRows();
    const bool selected =
        lichess.lobbySelection >= 0 && size_t(lichess.lobbySelection) < rows.size();
    switch (button) {
    case kLobbyOpen: {
        if (!selected)
            return;
        LobbyRow row = rows[size_t(lichess.lobbySelection)];
        if (row.challenge) {
            std::string id = lichess.challenges[row.index].id;
            lichess.challenges.erase(lichess.challenges.begin() + long(row.index));
            lichess.expectingGame = true;
            lichess.lobbyStatus = "Starting the game...";
            lichessRequest("accept", "POST", "/api/challenge/" + id + "/accept");
            return;
        }
        LichessState::LobbyGame game = lichess.games[row.index];
        if (!game.boardPlayable) {
            openUrl(lichess, baseUrl() + "/" + game.id);
            return;
        }
        if (lichess.inGame && game.id == lichess.gameId) {
            lichess.lobbyOpen = false;
            return;
        }
        if (lichess.inGame) {
            // Switching games: stop following the current one quietly.
            lichessCloseStream("game");
            finishLichessAnimation();
            lichess.inGame = false;
        }
        lichessJoinGame(game.id, game.color, game.opponent);
        return;
    }
    case kLobbyDecline: {
        LobbyRow row = rows[size_t(lichess.lobbySelection)];
        std::string id = lichess.challenges[row.index].id;
        lichess.challenges.erase(lichess.challenges.begin() + long(row.index));
        lichess.lobbySelection = std::max(0, std::min(lichess.lobbySelection, int(rows.size()) - 2));
        lichessRequest("decline", "POST", "/api/challenge/" + id + "/decline", "reason=generic");
        return;
    }
    case kLobbyTime: lichessMenuAction(kLichessTime); return;
    case kLobbyPlayAi:
        lichess.lobbyStatus = "Starting a game against the Lichess AI...";
        lichessMenuAction(kLichessPlayAi);
        return;
    case kLobbyChallenge: lichessMenuAction(kLichessChallenge); return;
    case kLobbySeek: lichessMenuAction(kLichessSeek); return;
    default:
        lichess.lobbyOpen = false;
        lichess.lobbyStatus.clear();
        return;
    }
}
