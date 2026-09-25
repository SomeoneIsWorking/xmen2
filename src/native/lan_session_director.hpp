#ifndef X2_LAN_SESSION_DIRECTOR_HPP
#define X2_LAN_SESSION_DIRECTOR_HPP

/*
 * Turning a running game into a LAN lobby, through the game's own front end.
 *
 * XMen2.exe cannot add a player to a level that is already loaded: a network
 * session always starts from the lobby, and every peer builds its world from
 * the host's start message (issue #188). What it can do is host a SAVED
 * campaign -- the host streams the save (messages 0x47/0x48) and every peer
 * loads it. So a drop-in re-forms the session around the campaign as it is
 * right now:
 *
 *   capture the running campaign            (CampaignSnapshot)
 *   leave to the front end                  ("mainmenuexit 1")
 *   online menu -> Ready                    (the menu's own handler)
 *   campaign lobby -> Host Game             (the menu's own handler)
 *   install the capture as the hosted save  (what load completion does)
 *   Game Options -> Post Game               (the menu's own handler)
 *   host menu: Start Game once every joined player is Ready
 *
 * A joiner runs the mirror image: leave to the main menu if playing, online
 * menu -> Ready, campaign lobby -> Join Game, join the game the browser lists,
 * Ready, and wait for the host's start.
 *
 * Each menu step focuses the named item and delivers one accept press, so the
 * retail handler performs the complete transition it always has. Each await
 * has a deadline; missing one fails the script by name rather than leaving
 * the game half way through a lobby.
 */

extern "C" {
#include "x86rt.h"
}

#include "campaign_snapshot.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace x2::lan {

struct ScriptAction {
  enum class Kind {
    CaptureCampaign,
    QueueCommand,
    AwaitMenu,
    Press,
    InstallCampaign,
    StartWhenReady,
    LeaveToMainMenu,
    AwaitListedGame,
    ReadyUp,
    AwaitGameStart
  };
  Kind kind;
  /* The command, menu, or item the action names. */
  std::string text;
  /* Press only: the menu the press opens. The press repeats until it does. */
  std::string opens;
};

class SessionDirector {
public:
  enum class Role { Host, Join };

  /* Any thread. Refused (false) while a script is already running. */
  bool request(Role role);

  /* The guest's input thread, once per poll. */
  void poll(const CPU &cpu, double now);

  /* Any thread: one line naming the script's position or its last outcome. */
  std::string status() const;

private:
  void poll_unguarded(const CPU &cpu, double now);
  void begin(Role role, double now);
  bool step(const CPU &cpu, double now);
  bool install_campaign(const CPU &cpu);
  bool start_when_ready(const CPU &cpu, double now);
  bool leave_to_main_menu(const CPU &cpu);
  bool ready_up(const CPU &cpu, double now);
  bool press(const CPU &cpu, const std::string &item, double now);
  void fail(const std::string &why);
  void set_status(std::string text);

  mutable std::mutex mutex_;
  bool requested_ = false;
  Role requested_role_ = Role::Host;
  bool polling_ = false;
  std::string status_ = "idle";

  std::vector<ScriptAction> script_;
  size_t next_ = 0;
  double deadline_ = 0.0;
  double next_attempt_ = 0.0;
  bool exit_queued_ = false;
  std::string last_menu_;
  save::CampaignSnapshot snapshot_;
};

SessionDirector &session_director();

} // namespace x2::lan

#endif
