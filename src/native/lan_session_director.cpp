#include "lan_session_director.hpp"

#include "guest_call.hpp"
#include "retail_front_end.hpp"
#include "retail_net_session.hpp"

extern "C" {
#include "dinput_fifo.h"
#include "x2_log.h"
}

#include <algorithm>
#include <array>
#include <utility>

namespace x2::lan {
namespace {

using Kind = ScriptAction::Kind;

/* A menu transition that loads a map (leaving a level) takes seconds even
   unpaced; a lobby waiting for joiners has no deadline at all. */
inline constexpr double kAwaitSeconds = 60.0;
inline constexpr double kRepressSeconds = 4.0;
/* The script a popup option runs to return to the main menu. */
inline constexpr const char *kLeaveScript = "mainMenuExit()";
inline constexpr double kReadyPollSeconds = 1.0;
/* How long an open lobby holds for a player it expects -- a client coming
   back from the game this re-form ended, or the player who asked -- before
   it starts with those who are Ready. */
inline constexpr double kLateJoinerSeconds = 45.0;

/* FUN_00608260(manager, save, flag): host this save; FUN_00608330(manager,
   name): the name the lobby shows for it. */
inline constexpr uint32_t kHostSaveRva = 0x00208260u;
inline constexpr uint32_t kHostSaveNameRva = 0x00208330u;
/* FUN_0060c7e0 -> FUN_0060b9b0(config, type): the session's game type. */
inline constexpr uint32_t kGameConfigRva = 0x0020c7e0u;
inline constexpr uint32_t kSetGameTypeRva = 0x0020b9b0u;
inline constexpr uint32_t kCampaignGameType = 1u;
/* FUN_0048fed0: the busy indicator load completion brackets itself with. */
inline constexpr uint32_t kBusyIndicatorRva = 0x0008fed0u;
inline constexpr uint32_t kBusyBeginSlot = 0x18u;
inline constexpr uint32_t kBusyEndSlot = 0x20u;
/* The game owner applies a save (slot 0x20c, mode 3), ends its load (0x280),
   and names its mode (0x268). */
inline constexpr uint32_t kGameOwnerRva = 0x0006dce0u;
inline constexpr uint32_t kApplySaveSlot = 0x20cu;
inline constexpr uint32_t kApplySaveMode = 3u;
inline constexpr uint32_t kEndLoadSlot = 0x280u;
inline constexpr uint32_t kGameModeSlot = 0x268u;
inline constexpr uint32_t kSaveAppliedFlagRva = 0x0030b70du;
inline constexpr uint32_t kFinishLoadRva = 0x000d17e0u;
/* Start Game itself refuses a lobby of one (FUN_005bacd0). */
inline constexpr uint32_t kMinimumPlayers = 2u;

/* Steps that wait on other machines' players, which no deadline bounds. */
bool waits_on_players(Kind kind) {
  return kind == Kind::StartWhenReady || kind == Kind::AwaitGameStart;
}

const char *kind_name(Kind kind) {
  switch (kind) {
  case Kind::CaptureCampaign:
    return "capture";
  case Kind::QueueCommand:
    return "command";
  case Kind::AwaitMenu:
    return "await";
  case Kind::Press:
    return "press";
  case Kind::InstallCampaign:
    return "install";
  case Kind::StartWhenReady:
    return "start";
  case Kind::LeaveToMainMenu:
    return "leave";
  case Kind::ResetHostInfo:
    return "host-info";
  case Kind::AwaitListedGame:
    return "browse";
  case Kind::ReadyUp:
    return "ready";
  case Kind::AwaitGameStart:
    return "wait-start";
  }
  return "?";
}

std::vector<ScriptAction> host_script() {
  return {
      {Kind::CaptureCampaign, "", ""},
      {Kind::QueueCommand, "mainmenuexit 1", ""},
      {Kind::AwaitMenu, "main", ""},
      {Kind::ResetHostInfo, "", ""},
      {Kind::QueueCommand, "openmenu online", ""},
      {Kind::AwaitMenu, "online", ""},
      {Kind::Press, "text_ready", "campaign_lobby"},
      {Kind::Press, "text_hostgame", "game_options"},
      {Kind::InstallCampaign, "", ""},
      /* The Xbox layout's Invite Slots item is the PC's Post Game. */
      {Kind::Press, "text_inviteslots", "host"},
      {Kind::StartWhenReady, "", ""},
  };
}

std::vector<ScriptAction> join_script() {
  return {
      {Kind::LeaveToMainMenu, "", ""},
      {Kind::QueueCommand, "openmenu online", ""},
      {Kind::AwaitMenu, "online", ""},
      {Kind::Press, "text_ready", "campaign_lobby"},
      {Kind::Press, "text_joingame", "player_game_options"},
      /* The search filters (Game Type, Difficulty) default to Any; the item
         named text_mapname is the menu's Search button. */
      {Kind::Press, "text_mapname", "games_list"},
      {Kind::AwaitListedGame, "", ""},
      {Kind::Press, "text_list", "join"},
      {Kind::ReadyUp, "", ""},
      {Kind::AwaitGameStart, "", ""},
  };
}

} // namespace

bool SessionDirector::request(Role role, uint32_t expected_players) {
  std::lock_guard lock(mutex_);
  if (requested_ || next_ < script_.size()) {
    return false;
  }
  requested_ = true;
  requested_role_ = role;
  expected_players_ = std::max(expected_players, kMinimumPlayers);
  return true;
}

std::string SessionDirector::status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

void SessionDirector::set_status(std::string text) {
  std::lock_guard lock(mutex_);
  status_ = std::move(text);
}

void SessionDirector::fail(const std::string &why) {
  x2_log_error("lan: the session script stopped: %s", why.c_str());
  set_status("failed: " + why);
  script_.clear();
  next_ = 0;
}

void SessionDirector::begin(Role role, double now) {
  role_ = role;
  script_ = role == Role::Host ? host_script() : join_script();
  next_ = 0;
  deadline_ = now + kAwaitSeconds;
  next_attempt_ = now;
  step_started_ = now;
  exit_queued_ = false;
  x2_log_info("lan: %s (%zu steps)",
              role == Role::Host ? "re-forming this game as a LAN lobby"
                                 : "joining the LAN game this machine finds",
              script_.size());
}

void SessionDirector::poll(const CPU &cpu, double now) {
  /* A step can run guest code that polls input again -- applying a save
     draws a loading screen -- and that poll must not start the step again. */
  if (polling_) {
    return;
  }
  polling_ = true;
  poll_unguarded(cpu, now);
  polling_ = false;
}

void SessionDirector::poll_unguarded(const CPU &cpu, double now) {
  const std::string menu = retail::FrontEnd::active_menu(cpu);
  if (menu != last_menu_) {
    x2_log_info("lan: front end menu \"%s\" -> \"%s\"", last_menu_.c_str(),
                menu.c_str());
    last_menu_ = menu;
  }
  bool start = false;
  Role role = Role::Host;
  {
    std::lock_guard lock(mutex_);
    start = requested_;
    role = requested_role_;
    requested_ = false;
  }
  if (start) {
    begin(role, now);
  }
  while (next_ < script_.size()) {
    const Kind kind = script_[next_].kind;
    const std::string text = script_[next_].text;
    set_status(std::string(kind_name(kind)) + " " + text + " (step " +
               std::to_string(next_ + 1) + " of " +
               std::to_string(script_.size()) + ")");
    if (!step(cpu, now)) {
      if (!script_.empty() && !waits_on_players(kind) && now > deadline_) {
        fail(std::string(kind_name(kind)) + " \"" + text +
             "\" did not complete within " +
             std::to_string(static_cast<int>(kAwaitSeconds)) +
             "s; the menu is \"" + last_menu_ + "\"");
      }
      return;
    }
    ++next_;
    deadline_ = now + kAwaitSeconds;
    next_attempt_ = now;
    step_started_ = now;
    if (next_ == script_.size()) {
      x2_log_info("lan: the lobby started the game");
      set_status("started");
      script_.clear();
      next_ = 0;
      return;
    }
  }
}

bool SessionDirector::step(const CPU &cpu, double now) {
  const ScriptAction &action = script_[next_];
  switch (action.kind) {
  case Kind::CaptureCampaign:
    if (!snapshot_.capture(cpu)) {
      fail("the running campaign could not be serialized");
      return false;
    }
    return true;
  case Kind::QueueCommand:
    if (!retail::FrontEnd::queue_command(cpu, action.text)) {
      fail("the console refused \"" + action.text + "\"");
      return false;
    }
    return true;
  case Kind::AwaitMenu:
    return last_menu_ == action.text;
  case Kind::Press:
    if (last_menu_ == action.opens) {
      return true;
    }
    if (now >= next_attempt_) {
      next_attempt_ = now + kRepressSeconds;
      press(cpu, action.text, now);
    }
    return false;
  case Kind::InstallCampaign:
    return install_campaign(cpu);
  case Kind::StartWhenReady:
    return start_when_ready(cpu, now);
  case Kind::LeaveToMainMenu:
    return leave_to_main_menu(cpu, now);
  case Kind::ResetHostInfo:
    retail::NetSession(cpu).reset_host_info();
    return true;
  case Kind::AwaitListedGame:
    return retail::NetSession(cpu).joinable_listed_games() != 0u;
  case Kind::ReadyUp:
    return ready_up(cpu, now);
  case Kind::AwaitGameStart:
    if (last_menu_ == "join") {
      return false;
    }
    if (last_menu_.empty() || last_menu_ == "loading") {
      return true;
    }
    fail("the join menu closed to \"" + last_menu_ +
         "\" before the host started");
    return false;
  }
  return false;
}

bool SessionDirector::leave_to_main_menu(const CPU &cpu, double now) {
  if (last_menu_ == "main") {
    return true;
  }
  /* A client whose host went away is looking at the game's own "lost
     connection" dialog (FUN_005f2220). Its No runs mainMenuExit(): choose it,
     so the dialog closes and leaves as it always does. */
  if (retail::FrontEnd::focus_popup_option(cpu, kLeaveScript)) {
    if (now >= next_attempt_) {
      next_attempt_ = now + kRepressSeconds;
      accept_popup(now);
    }
    return false;
  }
  if (!exit_queued_) {
    exit_queued_ = true;
    if (!retail::FrontEnd::queue_command(cpu, "mainmenuexit 1")) {
      fail("the console refused \"mainmenuexit 1\"");
    }
  }
  return false;
}

void SessionDirector::accept_popup(double now) {
  std::array<char, 128> why{};
  if (!dinput_inject_press("Return", now, 0.0, "lan", why.data(),
                           static_cast<int>(why.size()))) {
    x2_log_error("lan: accept on the popup was not delivered: %s", why.data());
    return;
  }
  x2_log_info("lan: chose the popup's %s", kLeaveScript);
}

bool SessionDirector::ready_up(const CPU &cpu, double now) {
  if (last_menu_ != "join") {
    fail("the join menu closed before this player was Ready");
    return false;
  }
  if (retail::NetSession(cpu).local_player_ready()) {
    return true;
  }
  if (now >= next_attempt_) {
    next_attempt_ = now + kRepressSeconds;
    press(cpu, "text_readygame", now);
  }
  return false;
}

bool SessionDirector::press(const CPU &cpu, const std::string &item,
                            double now) {
  if (!retail::FrontEnd::focus(cpu, item)) {
    x2_log_info("lan: menu \"%s\" has no item \"%s\" yet", last_menu_.c_str(),
                item.c_str());
    return false;
  }
  std::array<char, 128> why{};
  if (!dinput_inject_press("Return", now, 0.0, "lan", why.data(),
                           static_cast<int>(why.size()))) {
    x2_log_error("lan: accept on \"%s\" was not delivered: %s", item.c_str(),
                 why.data());
    return false;
  }
  x2_log_info("lan: pressed \"%s\" on menu \"%s\"", item.c_str(),
              last_menu_.c_str());
  return true;
}

bool SessionDirector::install_campaign(const CPU &cpu) {
  const uint32_t exe = x86_module_base("XMen2.exe");
  const uint32_t save = snapshot_.address();
  if (!exe || !save) {
    fail("no captured campaign to host");
    return false;
  }
  const retail::NetSession session(cpu);
  const uint32_t manager = session.manager();
  if (!session.online()) {
    fail("the net manager is not online");
    return false;
  }
  /* The same order load completion (0x004aed10) uses for a hosted save. */
  const uint32_t busy =
      guest::GuestCall(cpu).cdecl_call(exe + kBusyIndicatorRva);
  guest::GuestCall(cpu).virtual_call(busy, kBusyBeginSlot, {3u});
  guest::GuestCall(cpu).thiscall(exe + kHostSaveRva, manager, {save, 0u});
  const guest::GuestText name("LAN game in progress");
  guest::GuestCall(cpu).thiscall(exe + kHostSaveNameRva, manager,
                                 {name.address()});
  const uint32_t config =
      guest::GuestCall(cpu).cdecl_call(exe + kGameConfigRva);
  guest::GuestCall(cpu).thiscall(exe + kSetGameTypeRva, config,
                                 {kCampaignGameType});
  const uint32_t owner = guest::GuestCall(cpu).cdecl_call(exe + kGameOwnerRva);
  guest::GuestCall(cpu).virtual_call(owner, kApplySaveSlot,
                                     {save, kApplySaveMode});
  guest::GuestCall(cpu).virtual_call(owner, kEndLoadSlot, {0u});
  WR8(exe + kSaveAppliedFlagRva, 1u);
  guest::GuestCall(cpu).cdecl_call(exe + kFinishLoadRva);
  const uint32_t mode =
      guest::GuestCall(cpu).virtual_call(owner, kGameModeSlot);
  session.set_game_mode(static_cast<uint8_t>(mode));
  guest::GuestCall(cpu).virtual_call(busy, kBusyEndSlot, {1u});
  x2_log_info("lan: the captured campaign is the hosted save (mode %u)", mode);
  return true;
}

bool SessionDirector::start_when_ready(const CPU &cpu, double now) {
  if (last_menu_ != "host") {
    fail("the host menu closed before the game started");
    return false;
  }
  if (now < next_attempt_) {
    return false;
  }
  next_attempt_ = now + kReadyPollSeconds;
  const retail::NetSession session(cpu);
  if (!session.all_ready(kMinimumPlayers)) {
    return false;
  }
  const uint32_t count = session.player_count();
  const uint32_t expected = expected_players();
  if (count < expected) {
    if (now - step_started_ < kLateJoinerSeconds) {
      return false;
    }
    x2_log_info("lan: %u of %u expected players came within %.0f s; "
                "starting with them",
                count, expected, kLateJoinerSeconds);
  } else {
    x2_log_info("lan: all %u players are Ready; starting", count);
  }
  return press(cpu, "text_startgame", now);
}

namespace {
SessionDirector g_director;
} // namespace

SessionDirector &session_director() { return g_director; }

bool SessionDirector::hosting() const {
  {
    std::lock_guard lock(mutex_);
    if (requested_) {
      return requested_role_ == Role::Host;
    }
  }
  return directing() && role_ == Role::Host;
}

void SessionDirector::expect_players(uint32_t count) {
  std::lock_guard lock(mutex_);
  expected_players_ = std::max(expected_players_, count);
}

uint32_t SessionDirector::expected_players() const {
  std::lock_guard lock(mutex_);
  return expected_players_;
}

} // namespace x2::lan
