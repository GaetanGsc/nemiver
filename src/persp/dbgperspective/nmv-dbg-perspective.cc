// -*- c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4; -*-'
//Author: Dodji Seketeli, Jonathon Jongsma
/*
 *This file is part of the Nemiver project
 *
 *Nemiver is free software; you can redistribute
 *it and/or modify it under the terms of
 *the GNU General Public License as published by the
 *Free Software Foundation; either version 2,
 *or (at your option) any later version.
 *
 *Nemiver is distributed in the hope that it will
 *be useful, but WITHOUT ANY WARRANTY;
 *without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *See the GNU General Public License for more details.
 *
 *You should have received a copy of the
 *GNU General Public License along with Nemiver;
 *see the file COPYING.
 *If not, write to the Free Software Foundation,
 *Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *See COPYRIGHT file copyright information.
 */
#include "config.h"
#include <cstring>
// For OpenBSD
#include <sys/types.h>
// For OpenBSD
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <glib/gi18n.h>

#include <giomm/file.h>
#include <giomm/contenttype.h>

#include <gtksourceviewmm/init.h>
#include <gtksourceviewmm/languagemanager.h>
#include <gtksourceviewmm/styleschememanager.h>

#include <pangomm/fontdescription.h>
#include <gtkmm/clipboard.h>
#include <gtkmm/separatortoolitem.h>
#include <gdkmm/cursor.h>
#include <gdkmm/devicemanager.h>
#include <gtk/gtk.h>
#include "common/nmv-safe-ptr-utils.h"
#include "common/nmv-env.h"
#include "common/nmv-date-utils.h"
#include "common/nmv-str-utils.h"
#include "common/nmv-address.h"
#include "common/nmv-loc.h"
#include "common/nmv-proc-utils.h"
#include "nmv-sess-mgr.h"
#include "nmv-dbg-perspective.h"
#include "nmv-source-editor.h"
#include "nmv-ui-utils.h"
#include "nmv-run-program-dialog.h"
#include "nmv-load-core-dialog.h"
#include "nmv-locate-file-dialog.h"
#include "nmv-saved-sessions-dialog.h"
#include "nmv-proc-list-dialog.h"
#include "nmv-ui-utils.h"
#include "nmv-call-stack.h"
#include "nmv-spinner-tool-item.h"
#include "nmv-local-vars-inspector.h"
#include "nmv-expr-inspector.h"
#include "nmv-global-vars-inspector-dialog.h"
#include "nmv-terminal.h"
#include "nmv-breakpoints-view.h"
#include "nmv-open-file-dialog.h"
#include "nmv-i-conf-mgr.h"
#include "nmv-preferences-dialog.h"
#include "nmv-popup-tip.h"
#include "nmv-thread-list.h"
#include "nmv-expr-inspector-dialog.h"
#include "nmv-find-text-dialog.h"
#include "nmv-set-breakpoint-dialog.h"
#include "nmv-choose-overloads-dialog.h"
#include "nmv-remote-target-dialog.h"
#include "nmv-registers-view.h"
#include "nmv-call-function-dialog.h"
#include "nmv-conf-keys.h"
#ifdef WITH_MEMORYVIEW
#include "nmv-memory-view.h"
#endif // WITH_MEMORYVIEW
#include "nmv-watchpoint-dialog.h"
#include "nmv-debugger-utils.h"
#include "nmv-set-jump-to-dialog.h"
#include "nmv-dbg-perspective-default-layout.h"
#include "nmv-dbg-perspective-wide-layout.h"
#include "nmv-dbg-perspective-two-pane-layout.h"
#ifdef WITH_DYNAMICLAYOUT
#include "nmv-dbg-perspective-dynamic-layout.h"
#endif // WITH_DYNAMICLAYOUT
#include "nmv-layout-manager.h"
#include "nmv-expr-monitor.h"

using namespace std;
using namespace nemiver::common;
using namespace nemiver::debugger_utils;
using namespace nemiver::ui_utils;
using namespace Gsv;

NEMIVER_BEGIN_NAMESPACE (nemiver)

const char *SET_BREAKPOINT    = "nmv-set-breakpoint";
const char *LINE_POINTER      = "nmv-line-pointer";
const char *RUN_TO_CURSOR     = "nmv-run-to-cursor";
const char *STEP_INTO         = "nmv-step-into";
const char *STEP_OVER         = "nmv-step-over";
const char *STEP_OUT          = "nmv-step-out";

// labels for widget tabs in the status notebook
const char *CONTEXT_VIEW_TITLE           = _("Context");
const char *TARGET_TERMINAL_VIEW_TITLE   = _("Target Terminal");
const char *BREAKPOINTS_VIEW_TITLE       = _("Breakpoints");
const char *REGISTERS_VIEW_TITLE         = _("Registers");
const char *MEMORY_VIEW_TITLE            = _("Memory");
const char *EXPR_MONITOR_VIEW_TITLE      = _("Expression Monitor");

const char *CAPTION_SESSION_NAME = "captionname";
const char *SESSION_NAME = "sessionname";
const char *PROGRAM_NAME = "programname";
const char *PROGRAM_ARGS = "programarguments";
const char *PROGRAM_CWD = "programcwd";
const char *LAST_RUN_TIME = "lastruntime";
const char *REMOTE_TARGET = "remotetarget";
const char *SOLIB_PREFIX = "solibprefix";

const char *DBG_PERSPECTIVE_MOUSE_MOTION_DOMAIN =
                                "dbg-perspective-mouse-motion-domain";
const char *DISASSEMBLY_TITLE = "<Disassembly>";

static const int NUM_INSTR_TO_DISASSEMBLE = 20;

const char *DBG_PERSPECTIVE_DEFAULT_LAYOUT = "default-layout";

const Gtk::StockID STOCK_SET_BREAKPOINT (SET_BREAKPOINT);
const Gtk::StockID STOCK_LINE_POINTER (LINE_POINTER);
const Gtk::StockID STOCK_RUN_TO_CURSOR (RUN_TO_CURSOR);
const Gtk::StockID STOCK_STEP_INTO (STEP_INTO);
const Gtk::StockID STOCK_STEP_OVER (STEP_OVER);
const Gtk::StockID STOCK_STEP_OUT (STEP_OUT);

const char *PROG_ARG_SEPARATOR = "#DUMMY-SEP007#";

class DBGPerspective;

static void
gio_file_monitor_cb (const Glib::RefPtr<Gio::File>& file,
                     const Glib::RefPtr<Gio::File>& other_file,
                     Gio::FileMonitorEvent event,
                     DBGPerspective *a_persp);

class DBGPerspective : public IDBGPerspective, public sigc::trackable {
    //non copyable
    DBGPerspective (const IPerspective&);
    DBGPerspective& operator= (const IPerspective&);
    struct Priv;
    SafePtr<Priv> m_priv;

    friend void gio_file_monitor_cb (const Glib::RefPtr<Gio::File>& a_f,
                                     const Glib::RefPtr<Gio::File>& a_f2,
                                     Gio::FileMonitorEvent event,
                                     DBGPerspective *a_persp);

private:

    struct SlotedButton : Gtk::Button {
        UString file_path;
        DBGPerspective *perspective;

        SlotedButton () :
            Gtk::Button (),
            perspective (0)
        {}

        SlotedButton (const Gtk::StockID &a_id) :
            Gtk::Button (a_id),
            perspective (0)
        {}

        void on_clicked ()
        {
            if (perspective) {
                perspective->close_file (file_path);
            }
        }

        ~SlotedButton ()
        {
        }
    };

    struct PopupScrolledWindow : Gtk::ScrolledWindow {
        /// Try to keep the height of the ScrolledWindow the same as
        /// the widget it contains, so that the user doesn't have to
        /// scroll. This has a limit though. When we reach the screen
        /// border, we don't want the container to grow past the
        /// border. At that point, the user will have to scroll.
        virtual void get_preferred_height_vfunc (int &minimum_height,
                                                 int &natural_height) const
        {
            LOG_FUNCTION_SCOPE_NORMAL_DD
            NEMIVER_TRY

            if (!get_realized ()) {
                Gtk::ScrolledWindow::get_preferred_height_vfunc (minimum_height,
                                                                 natural_height);
                return;
            }

            Glib::RefPtr<const Gdk::Window> window = get_parent_window ();
            int window_x = 0, window_y = 0;
            window->get_position (window_x, window_y);

            const double C = 0.90;

            // The maximum height limit for the container
            int max_height = get_screen ()->get_height () * C - window_y;
            LOG_DD ("max height: " << max_height);

            const Gtk::Widget *child = get_child ();
            THROW_IF_FAIL (child);
            int child_minimum_height, child_natural_height;
            child->get_preferred_height (child_minimum_height,
                                         child_natural_height);

            // If the height of the container is too big so that
            // it overflows the max usable height, clip it.
            if (child_minimum_height > max_height) {
                minimum_height = max_height;
                natural_height = max_height;
            } else {
                minimum_height = child_minimum_height;
                natural_height = child_natural_height;
            }

            LOG_DD ("setting scrolled window height: " << minimum_height);

            NEMIVER_CATCH
        }
    };

    //************
    //<signal slots>
    //************
    void on_open_action ();
    void on_close_action ();
    void on_reload_action ();
    void on_find_action ();
    void on_execute_program_action ();
    void on_load_core_file_action ();
    void on_attach_to_program_action ();
    void on_connect_to_remote_target_action ();
    void on_detach_from_program_action ();
    void on_choose_a_saved_session_action ();
    void on_current_session_properties_action ();
    void on_copy_action ();
    void on_stop_debugger_action ();
    void on_run_action ();
    void on_save_session_action ();
    void on_next_action ();
    void on_step_into_action ();
    void on_step_out_action ();
    void on_step_in_asm_action ();
    void on_step_over_asm_action ();
    void on_continue_action ();
    void on_continue_until_action ();
    void on_jump_to_location_action ();
    void on_jump_to_current_location_action ();
    void on_jump_and_break_to_current_location_action ();
    void on_break_before_jump (const std::map<string,
                                              IDebugger::Breakpoint>&,
                               const Loc &a_loc);
    void on_set_breakpoint_action ();
    void on_set_breakpoint_using_dialog_action ();
    void on_set_watchpoint_using_dialog_action ();
    void on_refresh_locals_action ();
    void on_disassemble_action (bool a_show_asm_in_new_tab);
    void on_switch_to_asm_action ();
    void on_toggle_breakpoint_action ();
    void on_toggle_breakpoint_enabled_action ();
    void on_toggle_countpoint_action ();
    void on_inspect_expression_action ();
    void on_expr_monitoring_requested (const IDebugger::VariableSafePtr);
    void on_call_function_action ();
    void on_find_text_response_signal (int);
    void on_breakpoint_delete_action
                                (const IDebugger::Breakpoint& a_breakpoint);
    void on_breakpoint_go_to_source_action
                                (const IDebugger::Breakpoint& a_breakpoint);
    void on_thread_list_thread_selected_signal (int a_tid);

    void on_switch_page_signal (Gtk::Widget *a_page, guint a_page_num);

    void on_attached_to_target_signal (IDebugger::State a_state);

    void on_going_to_run_target_signal (bool);

    void on_sv_markers_region_clicked_signal
                                        (int a_line, bool a_dialog_requested,
                                         SourceEditor *a_editor);

    bool on_button_pressed_in_source_view_signal (GdkEventButton *a_event);

    bool on_popup_menu ();

    bool on_motion_notify_event_signal (GdkEventMotion *a_event);

    void on_leave_notify_event_signal (GdkEventCrossing *a_event);

    bool on_mouse_immobile_timer_signal ();

    void on_insertion_changed_signal (const Gtk::TextBuffer::iterator& iter,
                                      SourceEditor *a_editor);

    void update_toggle_menu_text (const UString& a_current_file,
                                  int a_current_line);

    void update_toggle_menu_text (const Address &a_address);

    void update_toggle_menu_text (const IDebugger::Breakpoint *);

    void update_toggle_menu_text (SourceEditor &);

    void update_toggle_menu_text (SourceEditor &,
                                  const Gtk::TextBuffer::iterator &);

    void on_shutdown_signal ();

    void on_conf_key_changed_signal (const UString &a_key,
                                     const UString &a_namespace);

    void on_debugger_connected_to_remote_target_signal ();

    void on_debugger_inferior_re_run_signal ();

    void on_debugger_detached_from_target_signal ();

    void on_debugger_got_target_info_signal (int a_pid,
                                             const UString &a_exe_path);

    void on_debugger_command_done_signal (const UString &a_command_name,
                                          const UString &a_cookie);

    void on_debugger_breakpoints_set_signal
    (const std::map<string, IDebugger::Breakpoint>&, const UString&);

    void on_debugger_bp_automatically_set_on_main
    (const map<string, IDebugger::Breakpoint>&, bool);

    void on_debugger_breakpoints_list_signal
                                (const map<string, IDebugger::Breakpoint> &,
                                 const UString &a_cookie);

    void on_debugger_breakpoint_deleted_signal
                                        (const IDebugger::Breakpoint&,
                                         const string&,
                                         const UString &a_cookie);

    void on_debugger_got_overloads_choice_signal
                    (const vector<IDebugger::OverloadsChoiceEntry> &entries,
                     const UString &a_cookie);

    void on_debugger_stopped_signal (IDebugger::StopReason a_reason,
                                     bool a_has_frame,
                                     const IDebugger::Frame &,
                                     int a_thread_id,
                                     const string&,
                                     const UString&);
    void on_program_finished_signal ();
    void on_engine_died_signal ();
    void on_frame_selected_signal (int, const IDebugger::Frame &);

    void on_debugger_running_signal ();

    void on_signal_received_by_target_signal (const UString &a_signal,
                                              const UString &a_meaning);

    void on_debugger_error_signal (const UString &a_msg);

    void on_debugger_state_changed_signal (IDebugger::State a_state);

    void on_debugger_variable_value_signal
                                    (const UString &a_var_name,
                                     const IDebugger::VariableSafePtr &a_var,
                                     const UString &a_cooker);

    void on_debugger_asm_signal1
                            (const common::DisassembleInfo &a_info,
                             const std::list<common::Asm> &a_instrs,
                             bool a_show_asm_in_new_tab = true);

    void on_debugger_asm_signal2
                            (const common::DisassembleInfo &info,
                             const std::list<common::Asm> &instrs,
                             SourceEditor *editor);

    void on_debugger_asm_signal3
                            (const common::DisassembleInfo &info,
                             const std::list<common::Asm> &instrs,
                             SourceEditor *editor,
                             const IDebugger::Breakpoint &a_bp);

    void on_debugger_asm_signal4
                            (const common::DisassembleInfo &info,
                             const std::list<common::Asm> &instrs,
                             const Address &address);

    void on_variable_created_for_tooltip_signal
                                    (const IDebugger::VariableSafePtr);
    void on_popup_tip_hide ();

    bool on_file_content_changed (const UString &a_path);
    void on_notebook_tabs_reordered(Gtk::Widget* a_page, guint a_page_num);

    void on_layout_changed ();
    void on_activate_context_view ();
    void on_activate_target_terminal_view ();
    void on_activate_breakpoints_view ();
    void on_activate_registers_view ();
#ifdef WITH_MEMORYVIEW
    void on_activate_memory_view ();
#endif // WITH_MEMORYVIEW
    void on_activate_expr_monitor_view ();
    void on_activate_global_variables ();
    void on_default_config_read ();

    //************
    //</signal slots>
    //************

    void update_action_group_sensitivity (IDebugger::State a_state);
    void update_copy_action_sensitivity ();
    string build_resource_path (const UString &a_dir, const UString &a_name);
    void add_stock_icon (const UString &a_stock_id,
                         const UString &icon_dir,
                         const UString &icon_name);
    void add_perspective_menu_entries ();
    void add_perspective_toolbar_entries ();
    void register_layouts ();
    void init_icon_factory ();
    void init_actions ();
    void init_toolbar ();
    void init_body ();
    void init_signals ();
    void init_debugger_signals ();
    void clear_status_notebook (bool a_restarting);
    void clear_session_data ();
    void append_source_editor (SourceEditor &a_sv,
                               const UString &a_path);
    SourceEditor* get_current_source_editor (bool a_load_if_nil = true);
    SourceEditor* get_source_editor_of_current_frame (bool a_bring_to_front = true);
    ISessMgr* session_manager_ptr ();
    UString get_current_file_path ();
    SourceEditor* get_source_editor_from_path (const UString& a_path,
                                               bool a_basename_only = false);
    SourceEditor* get_source_editor_from_path (const UString &a_path,
                                               UString &a_actual_file_path,
                                               bool a_basename_only=false);

    SourceEditor* get_or_append_source_editor_from_path (const UString &a_path);
    SourceEditor* get_or_append_asm_source_editor ();
    bool source_view_to_root_window_coordinates (int x, int y,
                                                 int &root_x,
                                                 int &root_y);
    IWorkbench& workbench () const;
    int get_num_notebook_pages ();
    SourceEditor* bring_source_as_current (const UString &a_path);
    void bring_source_as_current (SourceEditor *a_editor);
    void record_and_save_new_session ();
    void record_and_save_session (ISessMgr::Session &a_session);
    IProcMgr* get_process_manager ();
    void try_to_request_show_variable_value_at_position (int a_x, int a_y);
    void show_underline_tip_at_position (int a_x, int a_y,
                                         const UString &a_text);
    void show_underline_tip_at_position (int a_x, int a_y,
                                         IDebugger::VariableSafePtr a_var);
    ExprInspector& get_popup_expr_inspector ();
    void restart_mouse_immobile_timer ();
    void stop_mouse_immobile_timer ();
    PopupTip& get_popup_tip ();
    void hide_popup_tip_if_mouse_is_outside (int x, int y);
    FindTextDialog& get_find_text_dialog ();
    void add_views_to_layout ();

public:

    DBGPerspective (DynamicModule *a_dynmod);

    virtual ~DBGPerspective ();

    void do_init (IWorkbench *a_workbench);

    const UString& get_perspective_identifier ();

    void get_toolbars (list<Gtk::Widget*> &a_tbs);

    Gtk::Widget* get_body ();

    Gtk::Widget& get_source_view_widget ();

    IWorkbench& get_workbench ();

    void edit_workbench_menu ();

    SourceEditor* create_source_editor (Glib::RefPtr<Gsv::Buffer> &a_source_buf,
                                        bool a_asm_view,
                                        const UString &a_path,
                                        int a_current_line,
                                        const UString &a_current_address);

    void open_file ();

    bool open_file (const UString &a_path, int current_line=-1);

    SourceEditor* open_file_real (const UString &a_path, int current_line=-1);

    SourceEditor* open_file_real (const UString &a_path,
                                  int current_line,
                                  bool a_reload_visual_breakpoint);

    void close_current_file ();

    void find_in_current_file ();

    void close_file (const UString &a_path);

    Gtk::Widget* load_menu (const UString &a_filename,
                            const UString &a_widget_name);

    const char* get_asm_title ();

    bool load_asm (const common::DisassembleInfo &a_info,
                   const std::list<common::Asm> &a_asm,
                   Glib::RefPtr<Gsv::Buffer> &a_buf);

    SourceEditor* open_asm (const common::DisassembleInfo &a_info,
                            const std::list<common::Asm> &a_asm,
                            bool set_where = false);

    void switch_to_asm (const common::DisassembleInfo &a_info,
                        const std::list<common::Asm> &a_asm);

    void switch_to_asm (const common::DisassembleInfo &a_info,
                        const std::list<common::Asm> &a_asm,
                        SourceEditor *a_editor,
                        bool a_approximate_where = false);

    void pump_asm_including_address (SourceEditor *a_editor,
                                     const Address &a_address);

    void switch_to_source_code ();

    void close_opened_files ();

    void update_file_maps ();

    bool reload_file (const UString &a_file);
    bool reload_file ();

    ISessMgr& session_manager ();

    void execute_session (ISessMgr::Session &a_session);

    void execute_program ();

    void restart_inferior ();

    void restart_local_inferior ();

    void execute_program (const UString &a_prog,
                          const vector<UString> &a_args,
                          const map<UString, UString> &a_env,
                          const UString &a_cwd = ".",
                          bool a_close_opened_files = false,
                          bool a_break_in_main_run = true);

    void execute_program (const UString &a_prog,
                          const vector<UString> &a_args,
                          const map<UString, UString> &a_env,
                          const UString &a_cwd,
                          const vector<IDebugger::Breakpoint> &a_breaks,
                          bool a_restarting = true,
                          bool a_close_opened_files = false,
                          bool a_break_in_main_run = true);

    void attach_to_program ();
    void attach_to_program (unsigned int a_pid,
                            bool a_close_opened_files=false);
    void connect_to_remote_target ();

    void connect_to_remote_target (const UString &a_server_address,
                                   unsigned a_server_port,
                                   const UString &a_prog_path,
                                   const UString &a_solib_prefix);

    void connect_to_remote_target (const UString &a_serial_line,
                                   const UString &a_prog_path,
                                   const UString &a_solib_prefix);

	void connect_to_remote_target (const UString &a_command_line,
								   const UString &a_prog_path,
                                   const UString &a_solib_prefix);


    void reconnect_to_remote_target (const UString &a_remote_target,
                                     const UString &a_prog_path,
                                     const UString &a_solib_prefix);

    void pre_fill_remote_target_dialog (RemoteTargetDialog &a_dialog);

    bool is_connected_to_remote_target ();

    void detach_from_program ();
    void load_core_file ();
    void load_core_file (const UString &a_prog_file,
                         const UString &a_core_file_path);
    void save_current_session ();
    void choose_a_saved_session ();
    void edit_preferences ();

    void run ();
    void run_real (bool a_restarting);
    void stop ();
    void step_over ();
    void step_into ();
    void step_out ();
    void step_in_asm ();
    void step_over_asm ();
    void do_continue ();
    void do_continue_until ();
    void do_jump_to_current_location ();
    void do_jump_and_break_to_location (const Loc&);
    void do_jump_and_break_to_current_location ();
    void jump_to_location (const map<string, IDebugger::Breakpoint>&,
                           const Loc &);
    void jump_to_location_from_dialog (const SetJumpToDialog &);
    void set_breakpoint_at_current_line_using_dialog ();
    void set_breakpoint ();
    void set_breakpoint (const UString &a_file,
                         int a_line,
                         const UString &a_cond,
                         bool a_is_count_point);
    void set_breakpoint (const UString &a_func_name,
                         const UString &a_cond,
                         bool a_is_count_point);
    void set_breakpoint (const Address &a_address,
                         bool a_is_count_point);
    void set_breakpoint (const IDebugger::Breakpoint &a_breakpoint);
    void re_initialize_set_breakpoints ();
    void append_breakpoint (const IDebugger::Breakpoint &a_breakpoint);
    void append_breakpoints
                    (const map<string, IDebugger::Breakpoint> &a_breaks);

    const IDebugger::Breakpoint* get_breakpoint (const Loc&) const;
    const IDebugger::Breakpoint* get_breakpoint (const UString &a_file_name,
                                                 int a_linenum) const;
    const IDebugger::Breakpoint* get_breakpoint (const Address &) const;

    bool delete_breakpoint ();
    bool delete_breakpoint (const string &a_breakpoint_num);
    bool delete_breakpoint (const UString &a_file_path,
                            int a_linenum);
    bool delete_breakpoint (const Address &a_address);
    bool is_breakpoint_set_at_location (const Loc&, bool&);
    bool is_breakpoint_set_at_line (const UString &a_file_path,
                                    int a_linenum,
                                    bool &a_enabled);
    bool is_breakpoint_set_at_address (const Address &,
                                       bool&);
    void toggle_breakpoint (const UString &a_file_path,
                            int a_linenum);
    void toggle_breakpoint (const Address &a_address);
    void toggle_countpoint (const UString &a_file_path,
                            int a_linenum);
    void toggle_countpoint (const Address &a_address);
    void toggle_countpoint (void);
    void set_breakpoint_using_dialog ();
    void set_breakpoint_using_dialog (const UString &a_file_path,
                                      const int a_line_num);
    void set_breakpoint_using_dialog (const UString &a_function_name);
    void set_breakpoint_from_dialog (SetBreakpointDialog &a_dialog);
    void set_watchpoint_using_dialog ();
    bool breakpoint_and_frame_have_same_file (const IDebugger::Breakpoint&,
                                              const IDebugger::Frame&) const;

    bool get_frame_breakpoints_address_range (const IDebugger::Frame&,
                                              Range &) const;
    void refresh_locals ();
    void disassemble (bool a_show_asm_in_new_tab);
    void disassemble_and_do (IDebugger::DisassSlot &a_what_to_do,
                             bool a_tight = false);
    void disassemble_around_address_and_do (const Address &adress,
                                            IDebugger::DisassSlot &what_to_do);

    void inspect_expression ();
    void inspect_expression (const UString &a_variable_name);
    void call_function ();
    void call_function (const UString &a_call_expr);
    void toggle_breakpoint ();
    void toggle_breakpoint_enabled (const UString &a_file_path,
                                    int a_linenum);
    void toggle_breakpoint_enabled (const Address &a);
    void toggle_breakpoint_enabled (const string &a_break_num,
                                    bool a_enabled);
    void toggle_breakpoint_enabled ();

    void update_src_dependant_bp_actions_sensitiveness ();

    bool ask_user_to_select_file (const UString &a_file_name,
                                  UString& a_selected_file_path);

    bool append_visual_breakpoint (SourceEditor *editor,
                                   int linenum,
                                   bool is_countpoint,
                                   bool enabled);
    bool append_visual_breakpoint (SourceEditor *editor,
                                   const Address &address,
                                   bool is_countpoint,
                                   bool enabled);
    void delete_visual_breakpoint (const UString &a_file_name, int a_linenum);
    void delete_visual_breakpoint (map<string, IDebugger::Breakpoint>::iterator &a_i);
    void delete_visual_breakpoint (const string &a_breaknum);
    void delete_visual_breakpoints ();
    void choose_function_overload
                (const vector<IDebugger::OverloadsChoiceEntry> &a_entries);

    void remove_visual_decorations_from_text (const UString &a_file_path);
    bool apply_decorations (const UString &file_path);
    bool apply_decorations (SourceEditor *editor,
                            bool scroll_to_where_marker = false);
    bool apply_decorations_to_source (SourceEditor *a_editor,
                                      bool scroll_to_where_marker = false);
    bool apply_decorations_to_asm (SourceEditor *a_editor,
                                   bool a_scroll_to_where_marker = false,
                                   bool a_approximate_where = false);

    IDebuggerSafePtr& debugger ();

    IConfMgr& get_conf_mgr ();

    CallStack& get_call_stack ();

    Gtk::ScrolledWindow& get_call_stack_scrolled_win ();

    Gtk::ScrolledWindow& get_thread_list_scrolled_win ();

    Gtk::HPaned& get_context_paned ();

    Gtk::HPaned& get_call_stack_paned ();

    LocalVarsInspector& get_local_vars_inspector ();

    Gtk::ScrolledWindow& get_local_vars_inspector_scrolled_win ();

    Terminal& get_terminal ();

    Gtk::Box& get_terminal_box ();

    UString get_terminal_name ();

    Gtk::ScrolledWindow& get_breakpoints_scrolled_win ();

    BreakpointsView& get_breakpoints_view ();

    Gtk::ScrolledWindow& get_registers_scrolled_win ();

    RegistersView& get_registers_view ();

#ifdef WITH_MEMORYVIEW
    MemoryView& get_memory_view ();
#endif // WITH_MEMORYVIEW

    ExprMonitor& get_expr_monitor_view ();

    ThreadList& get_thread_list ();

    bool set_where (const IDebugger::Frame &a_frame,
                    bool a_do_scroll = true,
                    bool a_try_hard = false);

    bool set_where (const UString &a_path, int a_line,
                    bool a_do_scroll = true);

    bool set_where (SourceEditor *a_editor,
                    int a_line,
                    bool a_do_scroll);

    bool set_where (SourceEditor *a_editor,
                    const Address &a_address,
                    bool a_do_scroll = true,
                    bool a_try_hard = false,
                    bool a_approximate_where = false);

    void unset_where ();

    Gtk::Widget* get_contextual_menu ();

    void setup_and_popup_contextual_menu ();

    bool uses_launch_terminal () const;

    void uses_launch_terminal (bool a_flag);

    Gtk::Widget* get_call_stack_menu ();

    void read_default_config ();

    list<UString>& get_global_search_paths ();

    bool do_monitor_file (const UString &a_path);

    bool do_unmonitor_file (const UString &a_path);

    bool agree_to_shutdown ();

    sigc::signal<void, bool>& activated_signal ();
    sigc::signal<void, IDebugger::State>& attached_to_target_signal ();
    sigc::signal<void>& layout_changed_signal ();
    sigc::signal<void, bool>& going_to_run_target_signal ();
    sigc::signal<void>& default_config_read_signal ();
};//end class DBGPerspective

struct RefGObject {
    void operator () (Glib::Object *a_object)
    {
        if (a_object) {a_object->reference ();}
    }
};

struct UnrefGObject {
    void operator () (Glib::Object *a_object)
    {
        if (a_object) {a_object->unreference ();}
    }
};

struct RefGObjectNative {
    void operator () (void *a_object)
    {
        if (a_object && G_IS_OBJECT (a_object)) {
            g_object_ref (G_OBJECT (a_object));
        }
    }
};

struct UnrefGObjectNative {
    void operator () (void *a_object)
    {
        if (a_object && G_IS_OBJECT (a_object)) {
            g_object_unref (G_OBJECT (a_object));
        }
    }
};

static
void gio_file_monitor_cb (const Glib::RefPtr<Gio::File>& file,
                          const Glib::RefPtr<Gio::File>& other_file,
                          Gio::FileMonitorEvent event,
                          DBGPerspective *a_persp);

struct DBGPerspective::Priv {
    bool initialized;
    bool reused_session;
    bool debugger_has_just_run;
    // A Flag to know if the debugging
    // engine died or not.
    bool debugger_engine_alive;
    // The path to the program the user requested a debugging session
    // for.
    UString last_prog_path_requested;
    // The path to the binary that is actually being debugged.  This
    // can be different from last_prog_path_requested above, e.g, when
    // the user asked to debug a libtool wrapper shell script.  In
    // that case, this data member eventually contains the path to the
    // real underlying binary referred to by the libtool wrapper shell
    // script.
    UString prog_path;
    vector<UString> prog_args;
    UString prog_cwd;
    UString remote_target;
    UString solib_prefix;
    map<UString, UString> env_variables;
    list<UString> session_search_paths;
    list<UString> global_search_paths;
    map<UString, bool> paths_to_ignore;
    SafePtr<CallStack> call_stack;
    SafePtr<Gtk::ScrolledWindow> call_stack_scrolled_win;
    SafePtr<Gtk::ScrolledWindow> thread_list_scrolled_win;
    SafePtr<Gtk::HPaned> call_stack_paned;
    SafePtr<Gtk::HPaned> context_paned;

    Glib::RefPtr<Gtk::ActionGroup> default_action_group;
    Glib::RefPtr<Gtk::ActionGroup> inferior_loaded_action_group;
    Glib::RefPtr<Gtk::ActionGroup> detach_action_group;
    Glib::RefPtr<Gtk::ActionGroup> opened_file_action_group;
    Glib::RefPtr<Gtk::ActionGroup> debugger_ready_action_group;
    Glib::RefPtr<Gtk::ActionGroup> debugger_busy_action_group;
    Glib::RefPtr<Gtk::UIManager> ui_manager;
    Glib::RefPtr<Gtk::IconFactory> icon_factory;
    Gtk::UIManager::ui_merge_id menubar_merge_id;
    Gtk::UIManager::ui_merge_id toolbar_merge_id;
    Gtk::UIManager::ui_merge_id contextual_menu_merge_id;
    Gtk::Widget *contextual_menu;

    LayoutManager layout_mgr;
    IWorkbench *workbench;
    SafePtr<Gtk::HBox> toolbar;
    SafePtr<Gtk::Notebook> sourceviews_notebook;
    SafePtr<SpinnerToolItem> throbber;
    sigc::signal<void, bool> activated_signal;
    sigc::signal<void, IDebugger::State> attached_to_target_signal;
    sigc::signal<void, bool> going_to_run_target_signal;
    sigc::signal<void> default_config_read_signal;
    map<UString, int> path_2_pagenum_map;
    map<UString, int> basename_2_pagenum_map;
    map<int, SourceEditor*> pagenum_2_source_editor_map;
    map<int, UString> pagenum_2_path_map;
    typedef map<UString, Glib::RefPtr<Gio::FileMonitor> > Path2MonitorMap;
    Path2MonitorMap path_2_monitor_map;
    SafePtr<LocalVarsInspector> variables_editor;
    SafePtr<Gtk::ScrolledWindow> variables_editor_scrolled_win;
    SafePtr<Terminal> terminal;
    SafePtr<Gtk::Box> terminal_box;
    SafePtr<Gtk::ScrolledWindow> breakpoints_scrolled_win;
    SafePtr<BreakpointsView> breakpoints_view;
    SafePtr<ThreadList> thread_list;
    SafePtr<Gtk::ScrolledWindow> registers_scrolled_win;
    SafePtr<RegistersView> registers_view;
#ifdef WITH_MEMORYVIEW
    SafePtr<MemoryView> memory_view;
#endif // WITH_MEMORYVIEW
    SafePtr<ExprMonitor> expr_monitor;

    int current_page_num;
    IDebuggerSafePtr debugger;
    IDebugger::Frame current_frame;
    int current_thread_id;
    map<string, IDebugger::Breakpoint> breakpoints;
    ISessMgrSafePtr session_manager;
    ISessMgr::Session session;
    IProcMgrSafePtr process_manager;
    bool show_dbg_errors;
    bool use_system_font;
    bool show_line_numbers;
    bool confirm_before_reload_source;
    bool allow_auto_reload_source;
    bool enable_syntax_highlight;
    UString custom_font_name;
    UString system_font_name;
    bool use_launch_terminal;
    int num_instr_to_disassemble;
    bool asm_style_pure;
    bool enable_pretty_printing;
    bool pretty_printing_toggled;
    Glib::RefPtr<Gsv::StyleScheme> editor_style;
    sigc::connection timeout_source_connection;
    //**************************************
    //<detect mouse immobility > N seconds
    //**************************************
    int mouse_in_source_editor_x;
    int mouse_in_source_editor_y;
    //**************************************
    //</detect mouse immobility > N seconds
    //**************************************

    //****************************************
    //<variable value popup tip related data>
    //****************************************
    SafePtr<PopupTip> popup_tip;
    SafePtr<ExprInspector> popup_expr_inspector;
    bool in_show_var_value_at_pos_transaction;
    UString var_to_popup;
    int var_popup_tip_x;
    int var_popup_tip_y;
    //****************************************
    //</variable value popup tip related data>
    //****************************************

    //find text dialog
    FindTextDialogSafePtr find_text_dialog;

    list<UString> call_expr_history;
    list<UString> var_inspector_dialog_history;

    // This is set when the user presses a mouse button in the source
    // view
    GdkEventButton *source_view_event_button;

    Priv () :
        initialized (false),
        reused_session (false),
        debugger_has_just_run (false),
        debugger_engine_alive (false),
        menubar_merge_id (0),
        toolbar_merge_id (0),
        contextual_menu_merge_id(0),
        contextual_menu (0),
        workbench (0),
        current_page_num (0),
        current_thread_id (0),
        show_dbg_errors (false),
        use_system_font (true),
        show_line_numbers (true),
        confirm_before_reload_source (true),
        allow_auto_reload_source (true),
        enable_syntax_highlight (true),
        use_launch_terminal (false),
        num_instr_to_disassemble (NUM_INSTR_TO_DISASSEMBLE),
        asm_style_pure (true),
        enable_pretty_printing (true),
        pretty_printing_toggled (false),
        mouse_in_source_editor_x (0),
        mouse_in_source_editor_y (0),
        in_show_var_value_at_pos_transaction (false),
        var_popup_tip_x (0),
        var_popup_tip_y (0),
        source_view_event_button (0)
    {
    }

    Layout&
    layout ()
    {
        Layout *layout = layout_mgr.layout ();
        THROW_IF_FAIL (layout);
        return *layout;
    }

    void
    modify_source_editor_style (Glib::RefPtr<Gsv::StyleScheme> a_style_scheme)
    {
        if (!a_style_scheme) {
            LOG_ERROR ("Trying to set a style with null pointer");
            return;
        }
        map<int, SourceEditor*>::iterator it;
        for (it = pagenum_2_source_editor_map.begin ();
                it != pagenum_2_source_editor_map.end ();
                ++it) {
            if (it->second) {
                it->second->source_view ().get_source_buffer ()->set_style_scheme (a_style_scheme);
            }
        }
    }

    void
    modify_source_editor_fonts (const UString &a_font_name)
    {
        if (a_font_name.empty ()) {
            LOG_ERROR ("trying to set a font with empty name");
            return;
        }
        Pango::FontDescription font_desc (a_font_name);
        map<int, SourceEditor*>::iterator it;
        for (it = pagenum_2_source_editor_map.begin ();
                it != pagenum_2_source_editor_map.end ();
                ++it) {
            if (it->second) {
                it->second->source_view ().override_font (font_desc);
            }
        }
       THROW_IF_FAIL (terminal);
       terminal->modify_font (font_desc);
#ifdef WITH_MEMORYVIEW
        THROW_IF_FAIL (memory_view);
        memory_view->modify_font (font_desc);
#endif // WITH_MEMORYVIEW
    }

    Glib::RefPtr<Gsv::StyleScheme>
    get_editor_style ()
    {
        return editor_style;
    }

    Glib::ustring
    get_source_font_name ()
    {
        if (use_system_font) {
            return system_font_name;
        } else {
            return custom_font_name;
        }
    }

    bool
    get_supported_encodings (list<string> &a_encodings)
    {
        list<UString> encodings;

        NEMIVER_TRY;

        IConfMgrSafePtr conf_mgr = workbench->get_configuration_manager ();
        conf_mgr->get_key_value (CONF_KEY_SOURCE_FILE_ENCODING_LIST,
                                 encodings);

        NEMIVER_CATCH_AND_RETURN (false);

        for (list<UString>::const_iterator it = encodings.begin ();
             it != encodings.end ();
             ++it) {
            a_encodings.push_back (it->raw ());
        }
        return !encodings.empty ();
    }

    bool
    load_file (const UString &a_path,
               Glib::RefPtr<Gsv::Buffer> &a_buffer)
    {
        list<string> supported_encodings;
        get_supported_encodings (supported_encodings);
        return SourceEditor::load_file (a_path, supported_encodings,
                                        enable_syntax_highlight,
                                        a_buffer);
    }

    bool
    is_asm_title (const UString &a_path)
    {
        return (a_path.raw () == DISASSEMBLY_TITLE);
    }

    void
    build_find_file_search_path (list<UString> &a_search_path)
    {
        if (!prog_path.empty ())
            a_search_path.insert (a_search_path.end (),
                                  Glib::path_get_dirname (prog_path));

        if (!prog_cwd.empty ())
            a_search_path.insert (a_search_path.end (), prog_cwd);

        if (!session_search_paths.empty ())
            a_search_path.insert (a_search_path.end (),
                                  session_search_paths.begin (),
                                  session_search_paths.end ());

        if (!global_search_paths.empty ())
            a_search_path.insert (a_search_path.end (),
                                  global_search_paths.begin (),
                                  global_search_paths.end ());
    }

    bool
    find_file (const UString &a_file_name,
               UString &a_absolute_file_path)
    {
        list<UString> where_to_look;
        build_find_file_search_path (where_to_look);
        return env::find_file (a_file_name, where_to_look,
                               a_absolute_file_path);
    }

    /// Lookup a file path and return true if found. If the path is not
    /// absolute, look it up in the various directories we know about
    /// then return the absolute location at which it we found it.
    /// \param a_file_path the file path to look up.
    /// \param a_absolute_path the returned absolute location at which the
    /// file got found, iff the function returned true.
    /// \param a_ignore_if_not_found if true and if the file wasn't found
    /// *after* we asked the user [e.g because the user clicked 'cancel'
    /// in the dialog asking her to locate the file] subsequent calls to
    /// this function will not ask the user to locate the file again.
    /// \return true upon successful completion, false otherwise.
    bool
    find_file_or_ask_user (const UString &a_file_path,
                           UString &a_absolute_path,
                           bool a_ignore_if_not_found)
    {
        list<UString> where_to_look;
        build_find_file_search_path (where_to_look);
        return ui_utils::find_file_or_ask_user (a_file_path,
                                                where_to_look,
                                                session_search_paths,
                                                paths_to_ignore,
                                                a_ignore_if_not_found,
                                                a_absolute_path);
    }

    bool
    is_persistent_file (const UString &a_path)
    {
        if (is_asm_title (a_path))
            return false;
        UString absolute;
        if (find_file (a_path, absolute))
            return true;
        return false;
    }

};//end struct DBGPerspective::Priv

#ifndef CHECK_P_INIT
#define CHECK_P_INIT THROW_IF_FAIL(m_priv && m_priv->initialized);
#endif

static void
gio_file_monitor_cb (const Glib::RefPtr<Gio::File>& file,
                     const Glib::RefPtr<Gio::File>& other_file,
                     Gio::FileMonitorEvent event,
                     DBGPerspective *a_persp)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    RETURN_IF_FAIL (file);
    if (other_file) {}

    NEMIVER_TRY

    if (event == Gio::FILE_MONITOR_EVENT_CHANGED) {
        UString path = Glib::filename_to_utf8 (file->get_path ());
        Glib::signal_idle ().connect
            (sigc::bind
             (sigc::mem_fun (*a_persp,
                             &DBGPerspective::on_file_content_changed),
              path));
    }
    NEMIVER_CATCH
}

ostream&
operator<< (ostream &a_out,
            const IDebugger::Frame &a_frame)
{
    a_out << "file-full-name: " << a_frame.file_full_name () << "\n"
          << "file-name: "      << a_frame.file_name () << "\n"
          << "line number: "    << a_frame.line () << "\n";

    return a_out;
}

//****************************
//<slots>
//***************************
void
DBGPerspective::on_open_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    open_file ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_close_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    close_current_file ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_reload_action ()
{
    NEMIVER_TRY

    reload_file ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_find_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    find_in_current_file ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_execute_program_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    execute_program ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_load_core_file_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    load_core_file ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_attach_to_program_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    attach_to_program ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_connect_to_remote_target_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    connect_to_remote_target ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_detach_from_program_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    detach_from_program ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_choose_a_saved_session_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    choose_a_saved_session ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_current_session_properties_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    edit_preferences ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_copy_action ()
{
    if (SourceEditor *e = get_current_source_editor ()) {
        Glib::RefPtr<Gsv::Buffer> buffer =
            e->source_view ().get_source_buffer ();
        THROW_IF_FAIL (buffer);

        Gtk::TextIter start, end;
        if (buffer->get_selection_bounds (start, end))
            g_signal_emit_by_name (e->source_view ().gobj (),
                                   "copy-clipboard");
    }

}

void
DBGPerspective::on_run_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    run ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_save_session_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    save_current_session ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_stop_debugger_action (void)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    stop ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_next_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    step_over ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_step_into_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    step_into ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_step_out_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    step_out ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_step_in_asm_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    step_in_asm ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_step_over_asm_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    step_over_asm ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_continue_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    do_continue ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_continue_until_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    do_continue_until ();
    NEMIVER_CATCH
}

/// This function is called when the user activates the action
/// "JumpToLocationMenuItemAction".  You can Look at
/// DBGPerspective::init_actions to see how it is defined.  It
/// basically lets the user choose the location where she wants to
/// jump to, and instruct IDebugger to jump there.
void
DBGPerspective::on_jump_to_location_action ()
{
    SetJumpToDialog dialog (plugin_path ());

    SourceEditor *editor = get_current_source_editor ();

    // If the user has selected a current location (possibly to jump
    // to), then pre-fill the dialog with that location.
    SafePtr<const Loc> cur_loc;
    if (editor)
        cur_loc.reset (editor->current_location ());
    if (cur_loc)
        dialog.set_location (*cur_loc);

    // By default, set a breakpoint to the location we are jumping to,
    // so that execution stops after the jump.
    dialog.set_break_at_location (true);

    // Set the default file name to the file being currently visited,
    // so that when the user enters a blank file name, the dialog
    // knows what file name she is talking about.
    if (editor
        && editor->get_buffer_type () == SourceEditor::BUFFER_TYPE_SOURCE) {
        dialog.set_current_file_name (get_current_file_path ());
    }

    // Now run the dialog and if the user hit anything but the OK
    // button a the end, consider that she wants to cancel this
    // procedure.
    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK)
        return;

    // By now we should have a proper location to jump to, so really
    // perfom the jumping.
    jump_to_location_from_dialog (dialog);
}

/// Callback function invoked when the menu item action "jump to
/// current location" is activated.
///
/// It jumps to the location selected by the user in the source
/// editor.
void
DBGPerspective::on_jump_to_current_location_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY
    do_jump_to_current_location ();
    NEMIVER_CATCH
}

/// Callback function invoked when the menu item action "setting a
/// breakpoint to current lcoation and jump here" is activated.
///
/// It sets a breakpoint to the current location and jumps here.  So
/// is transfered to the location selected by the user in the source
/// editor and is stopped there.
void
DBGPerspective::on_jump_and_break_to_current_location_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY
    do_jump_and_break_to_current_location ();
    NEMIVER_CATCH
}

/// This callback is invoked right after a breakpoint is set as part
/// of a "set a breakpoint and jump there" process.
///
/// So this function jumps to the position given in parameter.
///
/// \param a_loc the location to jump to.  This is also the location
/// of the breakpoint that was set previously and which triggered this
/// callback.
void
DBGPerspective::on_break_before_jump
(const std::map<string, IDebugger::Breakpoint> &,
 const Loc &a_loc)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    
    NEMIVER_TRY;
    debugger ()->jump_to_position (a_loc, &null_default_slot);
    NEMIVER_CATCH;
}

void
DBGPerspective::on_toggle_breakpoint_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    toggle_breakpoint ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_set_breakpoint_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    set_breakpoint_using_dialog ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_set_breakpoint_using_dialog_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY
    set_breakpoint_at_current_line_using_dialog ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_set_watchpoint_using_dialog_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    set_watchpoint_using_dialog ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_refresh_locals_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    refresh_locals ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_disassemble_action (bool a_show_asm_in_new_tab)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    disassemble (a_show_asm_in_new_tab);
    NEMIVER_CATCH
}

void
DBGPerspective::on_switch_to_asm_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    NEMIVER_CATCH
}

void
DBGPerspective::on_toggle_breakpoint_enabled_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    toggle_breakpoint_enabled ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_toggle_countpoint_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    toggle_countpoint ();

    NEMIVER_CATCH;
}

void
DBGPerspective::on_inspect_expression_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    inspect_expression ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_expr_monitoring_requested
(const IDebugger::VariableSafePtr a_var)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    THROW_IF_FAIL (m_priv && m_priv->expr_monitor);

    m_priv->expr_monitor->add_expression (a_var);

    NEMIVER_CATCH;
}


void
DBGPerspective::on_call_function_action ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    call_function ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_find_text_response_signal (int a_response)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    if (a_response != Gtk::RESPONSE_OK) {
        get_find_text_dialog ().hide ();
        return;
    }

    SourceEditor * editor = get_current_source_editor ();
    if (editor == 0)
        return;

    UString search_str;
    FindTextDialog& find_text_dialog  = get_find_text_dialog ();
    find_text_dialog.get_search_string (search_str);
    if (search_str == "")
        return;

    Gtk::TextIter start, end;
    if (!editor->do_search (search_str, start, end,
                            find_text_dialog.get_match_case (),
                            find_text_dialog.get_match_entire_word (),
                            find_text_dialog.get_search_backward (),
                            find_text_dialog.clear_selection_before_search ())) {
        UString message;
        if (find_text_dialog.get_wrap_around ()) {
            message = _("Reached end of file");
            find_text_dialog.clear_selection_before_search (true);
        } else {
            message.printf (_("Could not find string %s"),
                            search_str.c_str ());
            find_text_dialog.clear_selection_before_search (false);
        }
        ui_utils::display_info (message);
    } else {
        find_text_dialog.clear_selection_before_search (false);
    }

    NEMIVER_CATCH
}

void
DBGPerspective::on_breakpoint_delete_action
                                    (const IDebugger::Breakpoint& a_breakpoint)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    delete_breakpoint (a_breakpoint.id ());
    NEMIVER_CATCH
}

void
DBGPerspective::on_breakpoint_go_to_source_action
                                (const IDebugger::Breakpoint& a_breakpoint)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    UString file_path = a_breakpoint.file_full_name ();
    if (file_path.empty ())
        file_path = a_breakpoint.file_name ();

    SourceEditor *source_editor =
        get_or_append_source_editor_from_path (file_path);
    bring_source_as_current (source_editor);

    if (source_editor) {
        SourceEditor::BufferType type = source_editor->get_buffer_type ();
        switch (type) {
            case SourceEditor::BUFFER_TYPE_SOURCE:
                source_editor->scroll_to_line (a_breakpoint.line ());
                break;
            case SourceEditor::BUFFER_TYPE_ASSEMBLY:
                if (source_editor->scroll_to_address
                    (a_breakpoint.address (),
                     /*approximate=*/false) == false)
                    source_editor = 0;
                break;
            case SourceEditor::BUFFER_TYPE_UNDEFINED:
                break;
        }
    }

    if (source_editor == 0) {
        IDebugger::DisassSlot scroll_to_address;
        scroll_to_address =
            sigc::bind (sigc::mem_fun
                            (this,
                             &DBGPerspective::on_debugger_asm_signal4),
                        a_breakpoint.address ());
        disassemble_around_address_and_do (a_breakpoint.address (),
                                           scroll_to_address);
    }
    NEMIVER_CATCH
}

void
DBGPerspective::on_thread_list_thread_selected_signal (int a_tid)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    THROW_IF_FAIL (m_priv);

    LOG_DD ("current tid: " << m_priv->current_thread_id);
    LOG_DD ("new tid: " << a_tid);
    if (m_priv->current_thread_id == a_tid)
        return;

    m_priv->current_thread_id = a_tid;
    get_local_vars_inspector ().show_local_variables_of_current_function
        (m_priv->current_frame);

    NEMIVER_CATCH;
}


void
DBGPerspective::on_switch_page_signal (Gtk::Widget *a_page,
                                       guint a_page_num)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    if (a_page) {}

    NEMIVER_TRY
    m_priv->current_page_num = a_page_num;
    LOG_DD ("current_page_num: " << m_priv->current_page_num);
    NEMIVER_CATCH
}

void
DBGPerspective::on_going_to_run_target_signal (bool a_restarting)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    clear_status_notebook (a_restarting);
    re_initialize_set_breakpoints ();
    clear_session_data ();

    NEMIVER_CATCH;
}

void
DBGPerspective::on_attached_to_target_signal (IDebugger::State a_state)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    update_action_group_sensitivity (a_state);

    NEMIVER_CATCH;
}

void
DBGPerspective::on_sv_markers_region_clicked_signal (int a_line,
                                                     bool a_dialog_requested,
                                                     SourceEditor *a_editor)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    if (m_priv->debugger->get_state () == IDebugger::NOT_STARTED
        || a_editor == 0)
        return;

    UString path;
    a_editor->get_path (path);

    if (a_dialog_requested) {
        // FIXME: Handle assembly view mode.
        set_breakpoint_using_dialog (path, a_line);
    } else {
        SourceEditor::BufferType type = a_editor->get_buffer_type ();
        switch (type) {
            case SourceEditor::BUFFER_TYPE_SOURCE:
                toggle_breakpoint (path, a_line);
                break;
            case SourceEditor::BUFFER_TYPE_ASSEMBLY: {
                Address address;
                if (!a_editor->assembly_buf_line_to_addr (a_line, address))
                    return;
                toggle_breakpoint (address);
            }
                break;
            case SourceEditor::BUFFER_TYPE_UNDEFINED:
                break;
        }
    }

    NEMIVER_CATCH;
}

bool
DBGPerspective::on_button_pressed_in_source_view_signal
                                                (GdkEventButton *a_event)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (a_event->type == GDK_BUTTON_PRESS) {
        m_priv->source_view_event_button = a_event;
        update_copy_action_sensitivity ();
        if (a_event->button == 3)
            setup_and_popup_contextual_menu ();
    }

    return false;
}

bool
DBGPerspective::on_popup_menu ()
{
    setup_and_popup_contextual_menu ();
    return true;
}

bool
DBGPerspective::on_motion_notify_event_signal (GdkEventMotion *a_event)
{
    LOG_FUNCTION_SCOPE_NORMAL_D(DBG_PERSPECTIVE_MOUSE_MOTION_DOMAIN);

    NEMIVER_TRY;

    // Mouse pointer coordinates relative to the source editor window
    int x = 0, y = 0;
    GdkModifierType state = (GdkModifierType) 0;

    if (a_event->is_hint) {
#if GTK_CHECK_VERSION (3, 0, 0)
          gdk_window_get_device_position
            (a_event->window,
             gdk_event_get_device (reinterpret_cast<GdkEvent*> (a_event)),
             &x, &y, &state);

#else
          gdk_window_get_pointer (a_event->window, &x, &y, &state);
#endif
    } else {
        x = (int) a_event->x;
        y = (int) a_event->y;
        state = (GdkModifierType) a_event->state;
    }

    LOG_D ("(x,y) => (" << (int)x << ", " << (int)y << ")",
           DBG_PERSPECTIVE_MOUSE_MOTION_DOMAIN);
    m_priv->mouse_in_source_editor_x = x;
    m_priv->mouse_in_source_editor_y = y;
    if (m_priv->debugger->get_state () != IDebugger::NOT_STARTED) {
        restart_mouse_immobile_timer ();
    }

    // If the popup tip is visible and if the mouse pointer
    // is outside of its window, hide said popup tip.
    if (m_priv->popup_tip
        && m_priv->popup_tip->get_display ()) {
            // Mouse pointer coordinates relative to the root window
            int x = 0, y = 0;
            m_priv->popup_tip->get_display ()->get_device_manager
                ()->get_client_pointer ()->get_position (x, y);
            hide_popup_tip_if_mouse_is_outside (x, y);
    }

    NEMIVER_CATCH;

    return false;
}

void
DBGPerspective::on_leave_notify_event_signal (GdkEventCrossing *a_event)
{
    LOG_FUNCTION_SCOPE_NORMAL_D(DBG_PERSPECTIVE_MOUSE_MOTION_DOMAIN);

    NEMIVER_TRY;

    
    if (a_event) {}
    stop_mouse_immobile_timer ();

    NEMIVER_CATCH;
}

bool
DBGPerspective::on_mouse_immobile_timer_signal ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    if (get_contextual_menu ()
        && get_contextual_menu ()->get_visible ()) {
        return false;
    }

    if (!debugger ()->is_attached_to_target ()) {
        return false;
    }

    try_to_request_show_variable_value_at_position
                                        (m_priv->mouse_in_source_editor_x,
                                         m_priv->mouse_in_source_editor_y);
    NEMIVER_CATCH;

    return false;
}

void
DBGPerspective::on_insertion_changed_signal
                                    (const Gtk::TextBuffer::iterator &a_it,
                                     SourceEditor *a_editor)
{
    NEMIVER_TRY;

    THROW_IF_FAIL (a_editor);

    update_toggle_menu_text (*a_editor, a_it);
    update_copy_action_sensitivity ();

    NEMIVER_CATCH;
}

void
DBGPerspective::update_toggle_menu_text (const Address &a_address)
{
    const IDebugger::Breakpoint *bp = get_breakpoint (a_address);
    update_toggle_menu_text (bp);
}

void
DBGPerspective::update_toggle_menu_text (const UString &a_current_file,
                                         int a_current_line)
{
    const IDebugger::Breakpoint *bp
        = get_breakpoint (a_current_file, a_current_line);

    update_toggle_menu_text (bp);
}

void
DBGPerspective::update_toggle_menu_text (SourceEditor &a_editor)
{
    switch (a_editor.get_buffer_type ()) {
    case SourceEditor::BUFFER_TYPE_SOURCE: {
        UString path;
        int line = -1;
        a_editor.get_path (path);
        a_editor.current_line (line);
        update_toggle_menu_text (path, line);
    }
        break;
    case SourceEditor::BUFFER_TYPE_ASSEMBLY: {
        Address a;
        if (a_editor.current_address (a))
            update_toggle_menu_text (a);
    }
        break;
    default:
        THROW ("should not be reached");
    }
}

void
DBGPerspective::update_toggle_menu_text (SourceEditor &a_editor,
                                         const Gtk::TextBuffer::iterator &a_it)
{
    int line = a_it.get_line () + 1;
    UString path;
    a_editor.get_path (path);

    switch (a_editor.get_buffer_type ()){
    case SourceEditor::BUFFER_TYPE_SOURCE:
        update_toggle_menu_text (path, line);
        break;
    case SourceEditor::BUFFER_TYPE_ASSEMBLY: {
        Address a;
        if (a_editor.assembly_buf_line_to_addr (line, a) == false) {
            LOG_DD ("No ASM @ at line " << line);
        }
        else
            update_toggle_menu_text (a);
    }
        break;
    default:
        THROW ("Should not be reached");
        break;
    }
}

void
DBGPerspective::update_toggle_menu_text (const IDebugger::Breakpoint *a_bp)
{
    Glib::RefPtr<Gtk::Action> toggle_enable_action =
        workbench ().get_ui_manager ()->get_action
        ("/MenuBar/MenuBarAdditions/DebugMenu/ToggleEnableBreakMenuItem");
    THROW_IF_FAIL (toggle_enable_action);
    Glib::RefPtr<Gtk::Action> toggle_break_action =
        workbench ().get_ui_manager ()->get_action
        ("/MenuBar/MenuBarAdditions/DebugMenu/ToggleBreakMenuItem");
    THROW_IF_FAIL (toggle_break_action);

    Glib::RefPtr<Gtk::Action> toggle_countpoint_action =
        workbench ().get_ui_manager ()->get_action
        ("/MenuBar/MenuBarAdditions/DebugMenu/ToggleCountpointMenuItem");

    toggle_enable_action->set_sensitive (a_bp != 0);

    if (a_bp != 0) {
        if (debugger ()->is_countpoint (*a_bp))
            toggle_countpoint_action->property_label () =
                _("Change to Standard Breakpoint");
        else
            toggle_countpoint_action->property_label () =
                _("Change to Countpoint");

        toggle_break_action->property_label () = _("Remove _Breakpoint");

        if (a_bp->enabled ()) {
            toggle_enable_action->property_label () = _("Disable Breakpoint");
        } else {
            toggle_enable_action->property_label () = _("Enable Breakpoint");
        }
    } else {
        toggle_break_action->property_label () = _("Set _Breakpoint");
        toggle_countpoint_action->property_label () =
            _("Set Countpoint");
    }
}

void
DBGPerspective::on_shutdown_signal ()
{
    IConfMgr &conf_mgr = get_conf_mgr ();
    int context_pane_location = get_context_paned ().get_position ();
    NEMIVER_TRY
    conf_mgr.set_key_value (CONF_KEY_CONTEXT_PANE_LOCATION,
                            context_pane_location);

    m_priv->layout ().save_configuration ();
    NEMIVER_CATCH_NOX

    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    if (m_priv->prog_path == "") {
        return;
    }

    // stop the debugger so that the target executable doesn't go on
    // running after we shut down
    debugger ()->exit_engine ();

    if (m_priv->reused_session) {
        record_and_save_session (m_priv->session);
        LOG_DD ("saved current session");
    } else {
        LOG_DD ("recorded a new session");
        record_and_save_new_session ();
    }

    NEMIVER_CATCH
}

/// Function called whenever a the value of a configuration key
/// changes on the sytem.
///
/// \param a_key the key which value changed
///
/// \param a_namespace the namespace of the key
void
DBGPerspective::on_conf_key_changed_signal (const UString &a_key,
                                            const UString &a_namespace)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    IConfMgr &conf_mgr = get_conf_mgr ();

    if (a_key == CONF_KEY_NEMIVER_SOURCE_DIRS) {
        LOG_DD ("updated key source-dirs");
        UString global_search_paths;
        conf_mgr.get_key_value (a_key, global_search_paths, a_namespace);
        m_priv->global_search_paths = global_search_paths.split_to_list (":");
    } else if (a_key == CONF_KEY_SHOW_DBG_ERROR_DIALOGS) {
        conf_mgr.get_key_value (a_key, m_priv->show_dbg_errors, a_namespace);
    } else if (a_key == CONF_KEY_SHOW_SOURCE_LINE_NUMBERS) {
        map<int, SourceEditor*>::iterator it;
        bool show_line_numbers = false;
        conf_mgr.get_key_value (a_key, show_line_numbers, a_namespace);
        for (it = m_priv->pagenum_2_source_editor_map.begin ();
             it != m_priv->pagenum_2_source_editor_map.end ();
             ++it) {
            if (it->second) {
                it->second->source_view ().set_show_line_numbers
                    (show_line_numbers);
            }
        }
    } else if (a_key == CONF_KEY_CONFIRM_BEFORE_RELOAD_SOURCE) {
        conf_mgr.get_key_value (a_key,
                                m_priv->confirm_before_reload_source,
                                a_namespace);
    } else if (a_key == CONF_KEY_ALLOW_AUTO_RELOAD_SOURCE) {
        conf_mgr.get_key_value (a_key,
                                m_priv->allow_auto_reload_source,
                                a_namespace);
    } else if (a_key == CONF_KEY_HIGHLIGHT_SOURCE_CODE) {
        map<int, SourceEditor*>::iterator it;
        bool highlight = false;
        conf_mgr.get_key_value (a_key, highlight, a_namespace);
        for (it = m_priv->pagenum_2_source_editor_map.begin ();
             it != m_priv->pagenum_2_source_editor_map.end ();
             ++it) {
            if (it->second && it->second->source_view ().get_buffer ()) {
                it->second->source_view ().get_source_buffer
                                                ()->set_highlight_syntax
                                                (highlight);
            }
        }
    } else if (a_key == CONF_KEY_USE_SYSTEM_FONT) {
        conf_mgr.get_key_value (a_key, m_priv->use_system_font, a_namespace);
        UString font_name;
        if (m_priv->use_system_font) {
            font_name = m_priv->system_font_name;
        } else {
            font_name = m_priv->custom_font_name;
        }
        if (!font_name.empty ())
            m_priv->modify_source_editor_fonts (font_name);
    } else if (a_key == CONF_KEY_CUSTOM_FONT_NAME) {
        conf_mgr.get_key_value (a_key, m_priv->custom_font_name, a_namespace);
        if (!m_priv->use_system_font && !m_priv->custom_font_name.empty ()) {
            m_priv->modify_source_editor_fonts (m_priv->custom_font_name);
        }
    } else if (a_key == CONF_KEY_SYSTEM_FONT_NAME) {
        // keep a cached copy of the system fixed-width font
        conf_mgr.get_key_value (a_key, m_priv->system_font_name, a_namespace);
        if (m_priv->use_system_font && !m_priv->system_font_name.empty ()) {
            m_priv->modify_source_editor_fonts (m_priv->system_font_name);
        }
    } else if (a_key == CONF_KEY_USE_LAUNCH_TERMINAL) {
        conf_mgr.get_key_value (a_key, m_priv->use_launch_terminal, a_namespace);
        if (m_priv->debugger_engine_alive) {
            debugger ()->set_tty_path (get_terminal_name ());
        }
    } else if (a_key == CONF_KEY_EDITOR_STYLE_SCHEME) {
        UString style_id;
        conf_mgr.get_key_value (a_key, style_id, a_namespace);
        if (!style_id.empty ()) {
            m_priv->editor_style =
                Gsv::StyleSchemeManager::get_default
                ()->get_scheme (style_id);
            m_priv->modify_source_editor_style (m_priv->editor_style);
        }
    } else if (a_key == CONF_KEY_DEFAULT_NUM_ASM_INSTRS) {
        // m_priv->num_instr_to_disassemble must never be NULL!
        int val = 0;
        conf_mgr.get_key_value (a_key, val, a_namespace);
        if (val != 0)
            m_priv->num_instr_to_disassemble = val;
    } else if (a_key == CONF_KEY_ASM_STYLE_PURE) {
        conf_mgr.get_key_value (a_key,
                                m_priv->asm_style_pure,
                                a_namespace);
    } else if (a_key == CONF_KEY_PRETTY_PRINTING) {
        bool e = false;
        conf_mgr.get_key_value (a_key, e, a_namespace);
        if (m_priv->enable_pretty_printing != e) {
            m_priv->enable_pretty_printing = e;
            m_priv->pretty_printing_toggled = true;
            get_local_vars_inspector ()
                .visualize_local_variables_of_current_function ();
        }
    }
    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_got_target_info_signal (int a_pid,
                                                    const UString &a_exe_path)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    THROW_IF_FAIL (m_priv);
    if (a_exe_path != "") {
        m_priv->prog_path = a_exe_path;
    }

    UString prog_info;
    prog_info.printf(_("%s (path=\"%s\", pid=%i)"),
            Glib::filename_display_basename(a_exe_path).c_str (),
            a_exe_path.c_str (), a_pid);
    workbench ().set_title_extension (prog_info);

    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_connected_to_remote_target_signal ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    ui_utils::display_info (_("Connected to remote target!"));
    debugger ()->list_breakpoints ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_inferior_re_run_signal ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    m_priv->debugger_has_just_run = true;

    NEMIVER_CATCH;
}

void
DBGPerspective::on_debugger_detached_from_target_signal ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    if (get_num_notebook_pages ())
        close_opened_files ();
    clear_status_notebook (true);
    workbench ().set_title_extension ("");
    //****************************
    //grey out all the menu
    //items but those to
    //to restart the debugger etc
    //***************************
    THROW_IF_FAIL (m_priv);
    m_priv->debugger_ready_action_group->set_sensitive (false);
    m_priv->debugger_busy_action_group->set_sensitive (false);
    m_priv->inferior_loaded_action_group->set_sensitive (false);

    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_command_done_signal (const UString &a_command,
                                                 const UString &a_cookie)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    LOG_DD ("a_command: " << a_command);
    LOG_DD ("a_cookie: " << a_cookie);

    NEMIVER_TRY;

    if (a_command == "attach-to-program") {
        debugger ()->step_over_asm ();
        debugger ()->get_target_info ();
    }

    NEMIVER_CATCH;
}

/// Callback function invoked once a breakpoint was set.
///
/// \param a_breaks the list of all breakpoints currently set in the inferior.
///
/// \param a_cookie a string passed to the IDebugger::set_breakpoint
/// call that triggered this callback.
///
void
DBGPerspective::on_debugger_breakpoints_set_signal
(const std::map<string, IDebugger::Breakpoint> &a,
 const UString&)
{
    NEMIVER_TRY;

    append_breakpoints (a);

    NEMIVER_CATCH;
}

/// This callback slot is invoked when a breakpoint is set.
///
/// The goal here is to detect that a breakpoint is actually set to
/// the 'main' entry point (that would mean that a 'main' entry point
/// function exists and one can set a breakpoint there).  In that
/// case, the function makes the debugger 'run', so that execution of
/// the inferior stops at the main entry point.
///
/// Otherwise, if no 'main' entry point exists or can be broke at, the
/// debugger does nothing.  The user will have to set a breakpoint at
/// a place of her choice and run the debugger so that it stops there.
///
/// \param a_bps the set of breakpoints that are set.
///
/// \param a_restarting whether the debugger is re-starting, or just
/// starting for the first time.
void
DBGPerspective::on_debugger_bp_automatically_set_on_main
(const map<string, IDebugger::Breakpoint>& a_bps,
 bool a_restarting)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD
    NEMIVER_TRY;

    for (map<string, IDebugger::Breakpoint>::const_iterator i = a_bps.begin ();
         i != a_bps.end ();
         ++i) {
        if (i->second.function () == "main"
            && !i->second.address ().empty ()) {
            run_real (a_restarting);
            return;
        }
    }

    NEMIVER_CATCH;
}

void
DBGPerspective::on_debugger_breakpoints_list_signal
                            (const map<string, IDebugger::Breakpoint> &a_breaks,
                             const UString &a_cookie)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY
    // if a breakpoint was stored in the session db as disabled,
    // it must be set initially and then immediately disabled.
    // When the breakpoint is set, it
    // will send a 'cookie' along of the form
    // "initiallly-disabled#filename.cc#123"
    if (a_cookie.find ("initially-disabled") != UString::npos) {
        UString::size_type start_of_file = a_cookie.find ('#') + 1;
        UString::size_type start_of_line = a_cookie.rfind ('#') + 1;
        UString file = a_cookie.substr (start_of_file,
                                        (start_of_line - 1) - start_of_file);
        int line = atoi
                (a_cookie.substr (start_of_line,
                                  a_cookie.size () - start_of_line).c_str ());
        map<string, IDebugger::Breakpoint>::const_iterator break_iter;
        for (break_iter = a_breaks.begin ();
             break_iter != a_breaks.end ();
             ++break_iter) {
            if ((break_iter->second.file_full_name () == file
                    || break_iter->second.file_name () == file)
                 && break_iter->second.line () == line) {
                debugger ()->disable_breakpoint (break_iter->second.id ());
            }
        }
    }
    LOG_DD ("debugger engine set breakpoints");
    append_breakpoints (a_breaks);
    SourceEditor* editor = get_current_source_editor ();
    if (!editor) {
        LOG_ERROR ("no editor was found");
        return;
    }
    update_toggle_menu_text (*editor);
    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_stopped_signal (IDebugger::StopReason a_reason,
                                            bool /*a_has_frame*/,
                                            const IDebugger::Frame &a_frame,
                                            int a_thread_id,
                                            const string &,
                                            const UString &)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    LOG_DD ("stopped, reason: " << (int)a_reason);

    THROW_IF_FAIL (m_priv);

    if (IDebugger::is_exited (a_reason))
        return;

    update_src_dependant_bp_actions_sensitiveness ();
    m_priv->current_frame = a_frame;
    m_priv->current_thread_id = a_thread_id;

    set_where (a_frame, /*do_scroll=*/true, /*try_hard=*/true);

    if (m_priv->debugger_has_just_run) {
        debugger ()->get_target_info ();
        m_priv->debugger_has_just_run = false;
    }

    NEMIVER_CATCH;
}

void
DBGPerspective::on_program_finished_signal ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    unset_where ();

    display_info (_("Program exited"));
    workbench ().set_title_extension ("");

    //****************************
    //grey out all the menu
    //items but those to
    //to restart the debugger etc
    //***************************
    update_action_group_sensitivity (IDebugger::PROGRAM_EXITED);

    //**********************
    //clear threads list and
    //call stack
    //**********************
    clear_status_notebook (true);
    NEMIVER_CATCH
}

void
DBGPerspective::on_engine_died_signal ()
{
    NEMIVER_TRY

    m_priv->debugger_engine_alive = false;

    m_priv->debugger_ready_action_group->set_sensitive (false);
    m_priv->debugger_busy_action_group->set_sensitive (false);
    m_priv->inferior_loaded_action_group->set_sensitive (false);

    ui_utils::display_info (_("The underlying debugger engine process died."));

    NEMIVER_CATCH

}

void
DBGPerspective::on_frame_selected_signal (int /* a_index */,
                                          const IDebugger::Frame &a_frame)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    if (m_priv->current_frame == a_frame)
    {
        // So the user clicked on the frame to select it, even if we
        // where already on it before.  That probably means that she
        // has scrolled the source view a little bit, lost where the
        // where-arrow was, and want to get it again.  So let's only
        // set the where and bail out.
        set_where (a_frame, /*a_do_scroll=*/true, /*a_try_hard=*/true);
        return;
    }

    m_priv->current_frame = a_frame;

    get_local_vars_inspector ().show_local_variables_of_current_function
                                                                    (a_frame);
    set_where (a_frame, /*a_do_scroll=*/true, /*a_try_hard=*/true);

    NEMIVER_CATCH
}


void
DBGPerspective::on_debugger_breakpoint_deleted_signal
                                        (const IDebugger::Breakpoint &,
                                         const string &a_break_number,
                                         const UString &)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    delete_visual_breakpoint (a_break_number);
    SourceEditor* editor = get_current_source_editor ();
    THROW_IF_FAIL (editor);
    update_toggle_menu_text (*editor);

    // Now delete the breakpoints and sub-breakpoints matching
    // a_break_number.
    map<string, IDebugger::Breakpoint>::iterator i,
        end = m_priv->breakpoints.end ();
    list<map<string, IDebugger::Breakpoint>::iterator> to_erase;
    list<map<string, IDebugger::Breakpoint>::iterator>::iterator j;
    for (i = m_priv->breakpoints.begin();i != end; ++i) {
        UString parent_id = i->second.parent_id ();
        if (parent_id == a_break_number
            || i->first == a_break_number)
            to_erase.push_back (i);
    }

    for (j = to_erase.begin (); j != to_erase.end (); ++j)
        m_priv->breakpoints.erase (*j);

    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_got_overloads_choice_signal
                    (const vector<IDebugger::OverloadsChoiceEntry> &entries,
                     const UString &a_cookie)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (a_cookie.empty ()) {}

    NEMIVER_TRY
    choose_function_overload (entries);
    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_running_signal ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY
    THROW_IF_FAIL (m_priv->throbber);
    workbench ().get_root_window ().get_window ()->set_cursor
                                                (Gdk::Cursor::create (Gdk::WATCH));
    m_priv->throbber->start ();
    NEMIVER_CATCH
}

void
DBGPerspective::on_signal_received_by_target_signal (const UString &a_signal,
                                                     const UString &a_meaning)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    UString message;
    // translators: first %s is the signal name, second one is the reason
    message.printf (_("Target received a signal: %s, %s"),
            a_signal.c_str (), a_meaning.c_str ());
    ui_utils::display_info (message);

    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_error_signal (const UString &a_msg)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    NEMIVER_TRY

    if (m_priv->show_dbg_errors) {
        UString message;
        message.printf (_("An error occurred: %s"), a_msg.c_str ());
        ui_utils::display_error (message);
    }

    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_state_changed_signal (IDebugger::State a_state)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    update_action_group_sensitivity (a_state);

    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_variable_value_signal
                                    (const UString &a_var_name,
                                     const IDebugger::VariableSafePtr &a_var,
                                     const UString &a_cookie)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    if (a_cookie.empty ()) {}

    NEMIVER_TRY
    THROW_IF_FAIL (m_priv);

    UString var_str;
    if (m_priv->in_show_var_value_at_pos_transaction
        && m_priv->var_to_popup == a_var_name) {
        a_var->to_string (var_str, true);
        show_underline_tip_at_position (m_priv->var_popup_tip_x,
                                        m_priv->var_popup_tip_y,
                                        var_str);
        m_priv->in_show_var_value_at_pos_transaction = false;
        m_priv->var_to_popup = "";
    }
    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_asm_signal1
                            (const common::DisassembleInfo &a_info,
                             const std::list<common::Asm> &a_instrs,
                             bool a_show_asm_in_new_tab)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    if (a_show_asm_in_new_tab) {
        open_asm (a_info, a_instrs, /*set_where=*/true);
    } else {
        switch_to_asm (a_info, a_instrs);
    }

    NEMIVER_CATCH
}

void
DBGPerspective::on_debugger_asm_signal2
                        (const common::DisassembleInfo &a_info,
                         const std::list<common::Asm> &a_instrs,
                         SourceEditor *a_editor)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    switch_to_asm (a_info, a_instrs, a_editor,
                   /*a_approximate_where=*/ true);

    NEMIVER_CATCH;
}

void
DBGPerspective::on_debugger_asm_signal3
                        (const common::DisassembleInfo &a_info,
                         const std::list<common::Asm> &a_instrs,
                         SourceEditor *a_editor,
                         const IDebugger::Breakpoint &a_bp)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    switch_to_asm (a_info, a_instrs, a_editor,
                   /*a_approximate_where=*/true);
    append_visual_breakpoint (a_editor, a_bp.address (),
                              debugger ()->is_countpoint (a_bp),
                              a_bp.line ());

    NEMIVER_CATCH;
}

void
DBGPerspective::on_debugger_asm_signal4
                        (const common::DisassembleInfo &a_info,
                         const std::list<common::Asm> &a_instrs,
                         const Address &a_address)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    SourceEditor* editor = open_asm (a_info, a_instrs, /*set_where=*/false);
    THROW_IF_FAIL (editor);
    bring_source_as_current (editor);
    editor->scroll_to_address (a_address,
                               /*approximate=*/true);

    NEMIVER_CATCH
}

void
DBGPerspective::on_variable_created_for_tooltip_signal
                                (const IDebugger::VariableSafePtr a_var)
{
    NEMIVER_TRY

    if (m_priv->in_show_var_value_at_pos_transaction
        && m_priv->var_to_popup == a_var->name ()) {
        show_underline_tip_at_position (m_priv->var_popup_tip_x,
                                        m_priv->var_popup_tip_y,
                                        a_var);
        m_priv->in_show_var_value_at_pos_transaction = false;
        m_priv->var_to_popup = "";
    }

    NEMIVER_CATCH
}

void
DBGPerspective::on_popup_tip_hide ()
{
    NEMIVER_TRY

    m_priv->popup_tip.reset ();
    m_priv->popup_expr_inspector.reset ();

    NEMIVER_CATCH
}

bool
DBGPerspective::on_file_content_changed (const UString &a_path)
{
    static std::list<UString> pending_notifications;
    LOG_DD ("file content changed");

    NEMIVER_TRY
    if (!a_path.empty ()) {
        //only notify for this path if there is not already a notification
        //pending
        if (std::find (pending_notifications.begin (),
                       pending_notifications.end (),
                       a_path)
            == pending_notifications.end ()) {
            pending_notifications.push_back (a_path);
            UString msg;
            msg.printf (_("File %s has been modified. "
                          "Do you want to reload it?"),
                        a_path.c_str ());
            bool dont_ask_again = !m_priv->confirm_before_reload_source;
            bool need_to_reload_file = m_priv->allow_auto_reload_source;
            if (!dont_ask_again) {
                if (ask_yes_no_question (msg,
                                         true /*propose to not ask again*/,
                                         dont_ask_again)
                        == Gtk::RESPONSE_YES) {
                    need_to_reload_file = true;
                } else {
                    need_to_reload_file = false;
                }
            }
            if (need_to_reload_file)
                reload_file ();
            LOG_DD ("don't ask again: " << (int) dont_ask_again);
            if (m_priv->confirm_before_reload_source == dont_ask_again) {
                NEMIVER_TRY
                get_conf_mgr ().set_key_value
                                (CONF_KEY_CONFIRM_BEFORE_RELOAD_SOURCE,
                                 !dont_ask_again);
                get_conf_mgr ().set_key_value
                                (CONF_KEY_ALLOW_AUTO_RELOAD_SOURCE,
                                 need_to_reload_file);
                NEMIVER_CATCH_NOX
            }
            std::list<UString>::iterator iter =
                std::find (pending_notifications.begin (),
                        pending_notifications.end (), a_path);
            if (iter != pending_notifications.end ()) {
                pending_notifications.erase (iter);
            }
        }
    }
    NEMIVER_CATCH
    return false;
}

void
DBGPerspective::on_notebook_tabs_reordered (Gtk::Widget* /*a_page*/,
                                            guint a_page_num)
{
    NEMIVER_TRY
    THROW_IF_FAIL (m_priv);
    update_file_maps ();
    m_priv->current_page_num = a_page_num;
    NEMIVER_CATCH
}

void
DBGPerspective::on_layout_changed ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    add_views_to_layout ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_activate_context_view ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY
    THROW_IF_FAIL (m_priv);
    m_priv->layout ().activate_view (CONTEXT_VIEW_INDEX);
    NEMIVER_CATCH
}

void
DBGPerspective::on_activate_target_terminal_view ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    THROW_IF_FAIL (m_priv);
    m_priv->layout ().activate_view (TARGET_TERMINAL_VIEW_INDEX);

    NEMIVER_CATCH
}

void
DBGPerspective::on_activate_breakpoints_view ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    THROW_IF_FAIL (m_priv);
    m_priv->layout ().activate_view (BREAKPOINTS_VIEW_INDEX);

    NEMIVER_CATCH
}

void
DBGPerspective::on_activate_registers_view ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    THROW_IF_FAIL (m_priv);
    m_priv->layout ().activate_view (REGISTERS_VIEW_INDEX);

    NEMIVER_CATCH
}

#ifdef WITH_MEMORYVIEW
void
DBGPerspective::on_activate_memory_view ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    THROW_IF_FAIL (m_priv);
    m_priv->layout ().activate_view (MEMORY_VIEW_INDEX);

    NEMIVER_CATCH
}
#endif //WITH_MEMORYVIEW

void
DBGPerspective::on_activate_expr_monitor_view ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY;

    THROW_IF_FAIL (m_priv);
    m_priv->layout ().activate_view (EXPR_MONITOR_VIEW_INDEX);

    NEMIVER_CATCH;
}

void
DBGPerspective::on_activate_global_variables ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    GlobalVarsInspectorDialog dialog (plugin_path (),
                                      debugger (),
                                      workbench ());
    dialog.run ();

    NEMIVER_CATCH
}

void
DBGPerspective::on_default_config_read ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    NEMIVER_TRY

    THROW_IF_FAIL (m_priv);
    if (!m_priv->get_source_font_name ().empty ()) {
        Pango::FontDescription font_desc (m_priv->get_source_font_name ());
#ifdef WITH_MEMORYVIEW
        get_memory_view ().modify_font (font_desc);
#endif // WITH_MEMORYVIEW
    }
    NEMIVER_CATCH
}

//****************************
//</slots>
//***************************

//*******************
//<private methods>
//*******************

/// Given a debugger state, update the sensitivity of the various menu
/// actions of the graphical debugger.
///
/// \param a_state the debugger state.
void
DBGPerspective::update_action_group_sensitivity (IDebugger::State a_state)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    LOG_DD ("state is '" << IDebugger::state_to_string (a_state) << "'");

    THROW_IF_FAIL (m_priv);
    THROW_IF_FAIL (m_priv->debugger_ready_action_group);
    THROW_IF_FAIL (m_priv->debugger_busy_action_group);
    THROW_IF_FAIL (m_priv->throbber);

    if (a_state == IDebugger::NOT_STARTED) {
        m_priv->throbber->stop ();
        // reset to default cursor
        workbench ().get_root_window ().get_window ()->set_cursor ();
        m_priv->default_action_group->set_sensitive (true);
        m_priv->detach_action_group->set_sensitive (false);
        m_priv->inferior_loaded_action_group->set_sensitive (false);
        m_priv->debugger_busy_action_group->set_sensitive (false);
        m_priv->debugger_ready_action_group->set_sensitive (false);
        if (get_num_notebook_pages ()) {
            close_opened_files ();
        }
    } else if (a_state == IDebugger::INFERIOR_LOADED) {
        // reset to default cursor
        workbench ().get_root_window ().get_window ()->set_cursor ();
        m_priv->detach_action_group->set_sensitive (false);
        m_priv->inferior_loaded_action_group->set_sensitive (true);
        m_priv->debugger_busy_action_group->set_sensitive (false);
        m_priv->debugger_ready_action_group->set_sensitive (false);
        m_priv->throbber->stop ();
    } else if (a_state == IDebugger::READY) {
        m_priv->throbber->stop ();
        // reset to default cursor
        workbench ().get_root_window ().get_window ()->set_cursor ();
        m_priv->detach_action_group->set_sensitive (true);
        m_priv->inferior_loaded_action_group->set_sensitive (true);
        m_priv->debugger_ready_action_group->set_sensitive (true);
        m_priv->debugger_busy_action_group->set_sensitive (false);
    } else if (a_state == IDebugger::RUNNING){
        m_priv->detach_action_group->set_sensitive (true);
        m_priv->inferior_loaded_action_group->set_sensitive (false);
        m_priv->debugger_ready_action_group->set_sensitive (false);
        m_priv->debugger_busy_action_group->set_sensitive (true);
    } else if (a_state == IDebugger::PROGRAM_EXITED) {
        m_priv->throbber->stop ();
        // reset to default cursor
        workbench ().get_root_window ().get_window ()->set_cursor ();
        m_priv->inferior_loaded_action_group->set_sensitive (true);
        m_priv->debugger_ready_action_group->set_sensitive (false);
        m_priv->debugger_busy_action_group->set_sensitive (false);
    }
}

/// Updates the sensitivity of the 'Copy' action item.
///
/// If there is something to copy, then the action is made sensitive
/// (i.e, clickable), otherwise it's made unsensitive.
void
DBGPerspective::update_copy_action_sensitivity ()
{
    Glib::RefPtr<Gtk::Action> action =
        m_priv->opened_file_action_group->get_action ("CopyMenuItemAction");

    if (!action)
        return;

    if (SourceEditor *e = get_current_source_editor ()) {
        Glib::RefPtr<Gsv::Buffer> buffer =
            e->source_view ().get_source_buffer ();
        if (!buffer)
            return;
        Gtk::TextIter start, end;
        bool sensitive = false;
        if (buffer->get_selection_bounds (start, end))
            sensitive = true;
        action->set_sensitive (sensitive);
    }
}

string
DBGPerspective::build_resource_path (const UString &a_dir,
                                     const UString &a_name)
{
    string relative_path =
        Glib::build_filename (Glib::filename_from_utf8 (a_dir),
                              Glib::filename_from_utf8 (a_name));
    string absolute_path;
    THROW_IF_FAIL (build_absolute_resource_path
                    (Glib::filename_to_utf8 (relative_path), absolute_path));
    return absolute_path;
}


void
DBGPerspective::add_stock_icon (const UString &a_stock_id,
                                const UString &a_icon_dir,
                                const UString &a_icon_name)
{
    if (!m_priv->icon_factory) {
        m_priv->icon_factory = Gtk::IconFactory::create ();
        m_priv->icon_factory->add_default ();
    }

    Gtk::StockID stock_id (a_stock_id);
    string icon_path = build_resource_path (a_icon_dir, a_icon_name);
    Glib::RefPtr<Gdk::Pixbuf> pixbuf =
                            Gdk::Pixbuf::create_from_file (icon_path);
    m_priv->icon_factory->add (stock_id, Gtk::IconSet::create (pixbuf));
}

void
DBGPerspective::add_perspective_menu_entries ()
{
    string relative_path = Glib::build_filename ("menus",
                                                 "menus.xml");
    string absolute_path;
    THROW_IF_FAIL (build_absolute_resource_path
                    (Glib::filename_to_utf8 (relative_path), absolute_path));

    m_priv->menubar_merge_id =
        workbench ().get_ui_manager ()->add_ui_from_file
                                    (Glib::filename_to_utf8 (absolute_path));

    relative_path = Glib::build_filename ("menus", "contextualmenu.xml");
    THROW_IF_FAIL (build_absolute_resource_path
                   (Glib::filename_to_utf8 (relative_path), absolute_path));
    m_priv->contextual_menu_merge_id =
        workbench ().get_ui_manager ()->add_ui_from_file
        (Glib::filename_to_utf8 (absolute_path));

#ifdef WITH_MEMORYVIEW
    // Add memory view menu item if we're compiling with memoryview support
    relative_path = Glib::build_filename ("menus", "memoryview-menu.xml");
    THROW_IF_FAIL (build_absolute_resource_path
                    (Glib::filename_to_utf8 (relative_path), absolute_path));
    workbench ().get_ui_manager ()->add_ui_from_file
                                (Glib::filename_to_utf8 (absolute_path));
#endif // WITH_MEMORYVIEW
}

void
DBGPerspective::add_perspective_toolbar_entries ()
{
    string relative_path = Glib::build_filename ("menus",
                                                 "toolbar.xml");
    string absolute_path;
    THROW_IF_FAIL (build_absolute_resource_path
                    (Glib::filename_to_utf8 (relative_path), absolute_path));

    m_priv->toolbar_merge_id =
        workbench ().get_ui_manager ()->add_ui_from_file
                                    (Glib::filename_to_utf8 (absolute_path));
}

void
DBGPerspective::init_icon_factory ()
{
    add_stock_icon (nemiver::SET_BREAKPOINT, "icons", "set-breakpoint.xpm");
    add_stock_icon (nemiver::LINE_POINTER, "icons", "line-pointer.png");
    add_stock_icon (nemiver::RUN_TO_CURSOR, "icons", "run-to-cursor.xpm");
    add_stock_icon (nemiver::STEP_INTO, "icons", "step-into.xpm");
    add_stock_icon (nemiver::STEP_OVER, "icons", "step-over.xpm");
    add_stock_icon (nemiver::STEP_OUT, "icons", "step-out.xpm");
}

void
DBGPerspective::init_actions ()
{
    Gtk::StockID nil_stock_id ("");
    sigc::slot<void> nil_slot;

    static ui_utils::ActionEntry s_detach_action_entries [] = {
        {
            "DetachFromProgramMenuItemAction",
            Gtk::Stock::DISCONNECT,
            _("_Detach From the Running Program"),
            _("Disconnect the debugger from the running target "
              "without killing it"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_detach_from_program_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
    };


    ui_utils::ActionEntry s_inferior_loaded_action_entries [] = {
        {
            "SaveSessionMenuItemAction",
            Gtk::Stock::SAVE,
            _("_Save Session to Disk"),
            _("Save the current debugging session to disk"),
            sigc::mem_fun (*this, &DBGPerspective::on_save_session_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "RunMenuItemAction",
            Gtk::Stock::REFRESH,
            _("_Run or Restart"),
            _("Run or Restart the target"),
            sigc::mem_fun (*this, &DBGPerspective::on_run_action),
            ActionEntry::DEFAULT,
            "<shift>F5",
            true
        },
        {
            "SetBreakpointUsingDialogMenuItemAction",
            nil_stock_id,
            _("Set Breakpoint with Dialog..."),
            _("Set a breakpoint at the current line using a dialog"),
            sigc::mem_fun
                (*this,
                 &DBGPerspective::on_set_breakpoint_using_dialog_action),
            ActionEntry::DEFAULT,
            "<control><shift>B",
            false
        },
        {
            "SetBreakpointMenuItemAction",
            nil_stock_id,
            _("Set Breakpoint..."),
            _("Set a breakpoint at a function or line number"),
            sigc::mem_fun (*this, &DBGPerspective::on_set_breakpoint_action),
            ActionEntry::DEFAULT,
            "<control>B",
            false
        },
        {
            "ToggleBreakpointMenuItemAction",
            nil_stock_id,
            // Depending on the context we will want this string to be
            // either "Set Breakpoint", or "Remove Breakpoint". Hence
            // this string is updated by
            // DBGPerspective::update_toggle_menu_text when needed. So
            // this initial value is going to be displayed only when
            // Nemiver is launched with no executable on the command
            // line.
            _("Toggle _Breakpoint"),
            _("Set/Unset a breakpoint at the current cursor location"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_toggle_breakpoint_action),
            ActionEntry::DEFAULT,
            "F8",
            false
        },
        {
            "ToggleEnableBreakpointMenuItemAction",
            nil_stock_id,
            // Depending on the context we will want this string to be
            // either "Enable Breakpoint", or "Disable
            // Breakpoint". Hence this string is updated by
            // DBGPerspective::update_toggle_menu_text when needed. So
            // this initial value is going to be displayed only when
            // Nemiver is launched with no executable on the command
            // line.
            _("Enable/Disable Breakpoint"),
            _("Enable or disable the breakpoint that is set at "
              "the current cursor location"),
            sigc::mem_fun
                    (*this,
                     &DBGPerspective::on_toggle_breakpoint_enabled_action),
            ActionEntry::DEFAULT,
            "<shift>F8",
            false
        },
        {
            "ToggleCountpointMenuItemAction",
            nil_stock_id,
            // Depending on the context we will want this string to be
            // either "Set Countpoint", or "Change to Countpoint", or
            // "Change to Standard Breakpoint". Hence
            // this string is updated by
            // DBGPerspective::update_toggle_menu_text when needed. So
            // this initial value is going to be displayed only when
            // Nemiver is launched with no executable on the command
            // line.
            _("Toggle _Countpoint"),
            _("Set/Unset a countpoint at the current cursor location"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_toggle_countpoint_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "SetWatchPointUsingDialogMenuItemAction",
            nil_stock_id,
            _("Set Watchpoint with Dialog..."),
            _("Set a watchpoint using a dialog"),
            sigc::mem_fun
                (*this,
                 &DBGPerspective::on_set_watchpoint_using_dialog_action),
            ActionEntry::DEFAULT,
            "<control>T",
            false
        },
    };


    static ui_utils::ActionEntry s_debugger_ready_action_entries [] = {
        {
            "NextMenuItemAction",
            nemiver::STOCK_STEP_OVER,
            _("_Next"),
            _("Execute next line stepping over the next function, if any"),
            sigc::mem_fun (*this, &DBGPerspective::on_next_action),
            ActionEntry::DEFAULT,
            "F6",
            false
        },
        {
            "StepMenuItemAction",
            nemiver::STOCK_STEP_INTO,
            _("_Step"),
            _("Execute next line, stepping into the next function, if any"),
            sigc::mem_fun (*this, &DBGPerspective::on_step_into_action),
            ActionEntry::DEFAULT,
            "F7",
            false
        },
        {
            "StepOutMenuItemAction",
            nemiver::STOCK_STEP_OUT,
            _("Step _Out"),
            _("Finish the execution of the current function"),
            sigc::mem_fun (*this, &DBGPerspective::on_step_out_action),
            ActionEntry::DEFAULT,
            "<shift>F7",
            false
        },
        {
            "StepInAsmMenuItemAction",
            nil_stock_id,
            _("Step Into asm"),
            _("Step into the next assembly instruction"),
            sigc::mem_fun (*this, &DBGPerspective::on_step_in_asm_action),
            ActionEntry::DEFAULT,
            "<control>I",
            false
        },
        {
            "StepOverAsmMenuItemAction",
            nil_stock_id,
            _("Step Over asm"),
            _("Step over the next assembly instruction"),
            sigc::mem_fun (*this, &DBGPerspective::on_step_over_asm_action),
            ActionEntry::DEFAULT,
            "<control>N",
            false
        },
        {
            "ContinueMenuItemAction",
            Gtk::Stock::EXECUTE,
            _("_Continue"),
            _("Continue program execution until the next breakpoint"),
            sigc::mem_fun (*this, &DBGPerspective::on_continue_action),
            ActionEntry::DEFAULT,
            "F5",
            true
        },
        {
            "ContinueUntilMenuItemAction",
            nil_stock_id,
            _("Run to Cursor"),
            _("Continue program execution until the currently selected "
              "line is reached"),
            sigc::mem_fun (*this, &DBGPerspective::on_continue_until_action),
            ActionEntry::DEFAULT,
            "F11",
            false
        },
        {
            "JumpToCurrentLocationMenuItemAction",
            nil_stock_id,
            _("Jump to Cursor"),
            _("Jump to the currently selected line"),
            sigc::mem_fun
            (*this, &DBGPerspective::on_jump_to_current_location_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "JumpAndBreakToCurrentLocationMenuItemAction",
            nil_stock_id,
            _("Jump and Stop to Cursor"),
            _("Sets a breakpoint to the currently "
              "selected line and jump there"),
            sigc::mem_fun
            (*this,
             &DBGPerspective::on_jump_and_break_to_current_location_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "JumpToLocationMenuItemAction",
            nil_stock_id,
            _("Jump to a Given Location"),
            _("Select a given code location and jump there"),
            sigc::mem_fun
            (*this,
             &DBGPerspective::on_jump_to_location_action),
            ActionEntry::DEFAULT,
            "<control>J",
            false
        },
        {
            "InspectExpressionMenuItemAction",
            nil_stock_id,
            _("Inspect an Expression"),
            _("Inspect a global or local expression"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_inspect_expression_action),
            ActionEntry::DEFAULT,
            "F12",
            false
        },
        {
            "CallFunctionMenuItemAction",
            nil_stock_id,
            _("Call a Function"),
            _("Call a function in the program being debugged"),
            sigc::mem_fun (*this, &DBGPerspective::on_call_function_action),
            ActionEntry::DEFAULT,
            "<control>E",
            false
        },
        {
            "ActivateGlobalVariablesDialogMenuAction",
            nil_stock_id,
            _("Show Global Variables"),
            _("Display all global variables"),
            sigc::mem_fun(*this,
                          &DBGPerspective::on_activate_global_variables),
            ActionEntry::DEFAULT,
            "<control>G",
            false
        },
        {
            "RefreshLocalVariablesMenuItemAction",
            nil_stock_id,
            _("Refresh Locals"),
            _("Refresh the list of variables local to the current function"),
            sigc::mem_fun (*this, &DBGPerspective::on_refresh_locals_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "DisAsmMenuItemAction",
            nil_stock_id,
            _("Show Assembly"),
            _("Show the assembly code of the source code being "
              "currently debugged, in another tab"),
            sigc::bind (sigc::mem_fun
                            (*this, &DBGPerspective::on_disassemble_action),
                        /*show_asm_in_new_tab=*/true),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "SwitchToAsmMenuItemAction",
            nil_stock_id,
            _("Switch to Assembly"),
            _("Show the assembly code of the source code being "
              "currently debugged"),
            sigc::bind (sigc::mem_fun
                            (*this, &DBGPerspective::on_disassemble_action),
                        /*show_asm_in_new_tab=*/false),
            ActionEntry::DEFAULT,
            "<control>A",
            false
        },
        {
            "SwitchToSourceMenuItemAction",
            nil_stock_id,
            _("Switch to Source"),
            _("Show the source code being currently debugged"),
            sigc::mem_fun (*this, &DBGPerspective::switch_to_source_code),
            ActionEntry::DEFAULT,
            "<control><shift>A",
            false
        },
    };

    static ui_utils::ActionEntry s_debugger_busy_action_entries [] = {
        {
            "StopMenuItemAction",
            Gtk::Stock::STOP,
            _("Stop"),
            _("Stop the debugger"),
            sigc::mem_fun (*this, &DBGPerspective::on_stop_debugger_action),
            ActionEntry::DEFAULT,
            "F9",
            true
        }
    };

    static ui_utils::ActionEntry s_default_action_entries [] = {
        {
            "ViewMenuAction",
            nil_stock_id,
            _("_View"),
            "",
            nil_slot,
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "ActivateTargetTerminalViewMenuAction",
            nil_stock_id,
            TARGET_TERMINAL_VIEW_TITLE,
            _("Switch to Target Terminal View"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_activate_target_terminal_view),
            ActionEntry::DEFAULT,
            "<alt>1",
            false
        },
        {
            "ActivateContextViewMenuAction",
            nil_stock_id,
            CONTEXT_VIEW_TITLE,
            _("Switch to Context View"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_activate_context_view),
            ActionEntry::DEFAULT,
            "<alt>2",
            false
        },
        {
            "ActivateBreakpointsViewMenuAction",
            nil_stock_id,
            BREAKPOINTS_VIEW_TITLE,
            _("Switch to Breakpoints View"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_activate_breakpoints_view),
            ActionEntry::DEFAULT,
            "<alt>3",
            false
        },
        {
            "ActivateRegistersViewMenuAction",
            nil_stock_id,
            REGISTERS_VIEW_TITLE,
            _("Switch to Registers View"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_activate_registers_view),
            ActionEntry::DEFAULT,
            "<alt>4",
            false
        },
#ifdef WITH_MEMORYVIEW
        {
            "ActivateMemoryViewMenuAction",
            nil_stock_id,
            MEMORY_VIEW_TITLE,
            _("Switch to Memory View"),
            sigc::mem_fun (*this, &DBGPerspective::on_activate_memory_view),
            ActionEntry::DEFAULT,
            "<alt>5",
            false
        },
#endif // WITH_MEMORYVIEW
         {
            "ActivateExprMonitorViewMenuAction",
            nil_stock_id,
            EXPR_MONITOR_VIEW_TITLE,
            _("Switch to Variables Monitor View"),
            sigc::mem_fun (*this, &DBGPerspective::on_activate_expr_monitor_view),
            ActionEntry::DEFAULT,
            "<alt>6",
            false
        },
        {
            "DebugMenuAction",
            nil_stock_id,
            _("_Debug"),
            "",
            nil_slot,
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "OpenMenuItemAction",
            Gtk::Stock::OPEN,
            _("_Open Source File..."),
            _("Open a source file for viewing"),
            sigc::mem_fun (*this, &DBGPerspective::on_open_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "ExecuteProgramMenuItemAction",
            nil_stock_id,
            _("Load _Executable..."),
            _("Execute a program"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_execute_program_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "LoadCoreMenuItemAction",
            nil_stock_id,
            _("_Load Core File..."),
            _("Load a core file from disk"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_load_core_file_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "AttachToProgramMenuItemAction",
            Gtk::Stock::CONNECT,
            _("_Attach to Running Program..."),
            _("Debug a program that's already running"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_attach_to_program_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "ConnectToRemoteTargetMenuItemAction",
            Gtk::Stock::NETWORK,
            _("_Connect to Remote Target..."),
            _("Connect to a debugging server to debug a remote target"),
            sigc::mem_fun
                    (*this,
                     &DBGPerspective::on_connect_to_remote_target_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "SavedSessionsMenuItemAction",
            nil_stock_id,
            _("Resume Sa_ved Session..."),
            _("Open a previously saved debugging session"),
            sigc::mem_fun (*this,
                           &DBGPerspective::on_choose_a_saved_session_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "CurrentSessionPropertiesMenuItemAction",
            Gtk::Stock::PREFERENCES,
            _("_Preferences"),
            _("Edit the properties of the current session"),
            sigc::mem_fun
                    (*this,
                     &DBGPerspective::on_current_session_properties_action),
            ActionEntry::DEFAULT,
            "",
            false
        }
    };

    static ui_utils::ActionEntry s_file_opened_action_entries [] = {
        {
            "CopyMenuItemAction",
            Gtk::Stock::COPY,
            _("_Copy selected text"),
            _("Copy the text selected in the current source file"),
            sigc::mem_fun (*this, &DBGPerspective::on_copy_action),
            ActionEntry::DEFAULT,
            "<control>C",
            false
        },
        {
            "ReloadSourceMenuItemAction",
            Gtk::Stock::REFRESH,
            _("_Reload Source File"),
            _("Reloads the source file"),
            sigc::mem_fun (*this, &DBGPerspective::on_reload_action),
            ActionEntry::DEFAULT,
            "<control>R",
            false
        },
        {
            "CloseMenuItemAction",
            Gtk::Stock::CLOSE,
            _("_Close Source File"),
            _("Close the opened file"),
            sigc::mem_fun (*this, &DBGPerspective::on_close_action),
            ActionEntry::DEFAULT,
            "",
            false
        },
        {
            "FindMenuItemAction",
            Gtk::Stock::FIND,
            _("_Find"),
            _("Find a text string in file"),
            sigc::mem_fun (*this, &DBGPerspective::on_find_action),
            ActionEntry::DEFAULT,
            "<Control>F",
            false
        }
    };

    m_priv->detach_action_group =
        Gtk::ActionGroup::create ("detach-action-group");
    m_priv->detach_action_group->set_sensitive (false);

    m_priv->inferior_loaded_action_group =
        Gtk::ActionGroup::create ("inferior-loaded-action-group");
    m_priv->inferior_loaded_action_group->set_sensitive (false);

    m_priv->debugger_ready_action_group =
                Gtk::ActionGroup::create ("debugger-ready-action-group");
    m_priv->debugger_ready_action_group->set_sensitive (false);

    m_priv->debugger_busy_action_group =
                Gtk::ActionGroup::create ("debugger-busy-action-group");
    m_priv->debugger_busy_action_group->set_sensitive (false);

    m_priv->default_action_group =
                Gtk::ActionGroup::create ("debugger-default-action-group");
    m_priv->default_action_group->set_sensitive (true);

    m_priv->opened_file_action_group =
                Gtk::ActionGroup::create ("opened-file-action-group");
    m_priv->opened_file_action_group->set_sensitive (false);


    ui_utils::add_action_entries_to_action_group
        (s_detach_action_entries,
         G_N_ELEMENTS (s_detach_action_entries),
         m_priv->detach_action_group);


    ui_utils::add_action_entries_to_action_group
                        (s_inferior_loaded_action_entries,
                         G_N_ELEMENTS (s_inferior_loaded_action_entries),
                         m_priv->inferior_loaded_action_group);

    ui_utils::add_action_entries_to_action_group
                        (s_debugger_ready_action_entries,
                         G_N_ELEMENTS (s_debugger_ready_action_entries),
                         m_priv->debugger_ready_action_group);

    ui_utils::add_action_entries_to_action_group
                        (s_debugger_busy_action_entries,
                         G_N_ELEMENTS (s_debugger_busy_action_entries),
                         m_priv->debugger_busy_action_group);

    ui_utils::add_action_entries_to_action_group
                        (s_default_action_entries,
                         G_N_ELEMENTS (s_default_action_entries),
                         m_priv->default_action_group);

    ui_utils::add_action_entries_to_action_group
                        (s_file_opened_action_entries,
                         G_N_ELEMENTS (s_file_opened_action_entries),
                         m_priv->opened_file_action_group);

    workbench ().get_ui_manager ()->insert_action_group
        (m_priv->detach_action_group);
    workbench ().get_ui_manager ()->insert_action_group
                                    (m_priv->inferior_loaded_action_group);
    workbench ().get_ui_manager ()->insert_action_group
                                    (m_priv->debugger_busy_action_group);
    workbench ().get_ui_manager ()->insert_action_group
                                    (m_priv->debugger_ready_action_group);
    workbench ().get_ui_manager ()->insert_action_group
                                    (m_priv->default_action_group);
    workbench ().get_ui_manager ()->insert_action_group
                                    (m_priv->opened_file_action_group);

    workbench ().get_root_window ().add_accel_group
        (workbench ().get_ui_manager ()->get_accel_group ());
}

void
DBGPerspective::init_toolbar ()
{
    add_perspective_toolbar_entries ();

    m_priv->throbber.reset (new SpinnerToolItem);
    m_priv->toolbar.reset ((new Gtk::HBox));
    THROW_IF_FAIL (m_priv->toolbar);
    Gtk::Toolbar *glade_toolbar = dynamic_cast<Gtk::Toolbar*>
            (workbench ().get_ui_manager ()->get_widget ("/ToolBar"));
    THROW_IF_FAIL (glade_toolbar);
    Glib::RefPtr<Gtk::StyleContext> style_context =
            glade_toolbar->get_style_context ();
    if (style_context) {
        style_context->add_class (GTK_STYLE_CLASS_PRIMARY_TOOLBAR);
    }
    Gtk::SeparatorToolItem *sep = Gtk::manage (new Gtk::SeparatorToolItem);
    gtk_separator_tool_item_set_draw (sep->gobj (), false);
    sep->set_expand (true);
    glade_toolbar->insert (*sep, -1);
    glade_toolbar->insert (*m_priv->throbber, -1);
    m_priv->toolbar->pack_start (*glade_toolbar);
    m_priv->toolbar->show_all ();
}

void
DBGPerspective::init_body ()
{
    IConfMgr &conf_mgr = get_conf_mgr ();

    get_thread_list_scrolled_win ().add (get_thread_list ().widget ());
    get_call_stack_paned ().add1 (get_thread_list_scrolled_win ());
    get_call_stack_scrolled_win ().add (get_call_stack ().widget ());
    get_call_stack_paned ().add2 (get_call_stack_scrolled_win ());

    get_context_paned ().pack1 (get_call_stack_paned ());
    get_context_paned ().pack2 (get_local_vars_inspector_scrolled_win ());

    int context_pane_location = -1;
    NEMIVER_TRY
    conf_mgr.get_key_value (CONF_KEY_CONTEXT_PANE_LOCATION,
                            context_pane_location);
    NEMIVER_CATCH_NOX

    if (context_pane_location > 0) {
        get_context_paned ().set_position (context_pane_location);
    }

    get_local_vars_inspector_scrolled_win ().add
                                    (get_local_vars_inspector ().widget ());
    get_breakpoints_scrolled_win ().add (get_breakpoints_view ().widget());
    get_registers_scrolled_win ().add (get_registers_view ().widget());

    m_priv->sourceviews_notebook.reset (new Gtk::Notebook);
    m_priv->sourceviews_notebook->remove_page ();
    m_priv->sourceviews_notebook->set_show_tabs ();
    m_priv->sourceviews_notebook->set_scrollable ();
#if GTK_CHECK_VERSION (2, 10, 0)
    m_priv->sourceviews_notebook->signal_page_reordered ().connect
        (sigc::mem_fun (this, &DBGPerspective::on_notebook_tabs_reordered));
#endif

    UString layout = DBG_PERSPECTIVE_DEFAULT_LAYOUT;
    NEMIVER_TRY
    conf_mgr.get_key_value (CONF_KEY_DBG_PERSPECTIVE_LAYOUT, layout);

    // If the layout is not registered, we use the default layout
    if (!m_priv->layout_mgr.is_layout_registered (layout)) {
        layout = DBG_PERSPECTIVE_DEFAULT_LAYOUT;
    }
    NEMIVER_CATCH_NOX

    m_priv->layout_mgr.load_layout (layout, *this);
    add_views_to_layout ();
}

void
DBGPerspective::init_signals ()
{
    m_priv->sourceviews_notebook->signal_switch_page ().connect
        (sigc::mem_fun (*this, &DBGPerspective::on_switch_page_signal));

    going_to_run_target_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_going_to_run_target_signal));

    attached_to_target_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_attached_to_target_signal));

    get_call_stack ().frame_selected_signal ().connect
        (sigc::mem_fun (*this, &DBGPerspective::on_frame_selected_signal));

    get_breakpoints_view ().go_to_breakpoint_signal ().connect
        (sigc::mem_fun (*this,
                        &DBGPerspective::on_breakpoint_go_to_source_action));

    get_thread_list ().thread_selected_signal ().connect (sigc::mem_fun
        (*this, &DBGPerspective::on_thread_list_thread_selected_signal));

    default_config_read_signal ().connect (sigc::mem_fun (this,
                &DBGPerspective::on_default_config_read));

    m_priv->layout_mgr.layout_changed_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_layout_changed));
}

/// Connect slots (callbacks) to the signals emitted by the
/// IDebugger interface whenever something worthwhile happens.
void
DBGPerspective::init_debugger_signals ()
{
    debugger ()->connected_to_server_signal ().connect (sigc::mem_fun
            (*this,
             &DBGPerspective::on_debugger_connected_to_remote_target_signal));

    debugger ()->detached_from_target_signal ().connect (sigc::mem_fun
        (*this, &DBGPerspective::on_debugger_detached_from_target_signal));

    debugger ()->command_done_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_command_done_signal));

    debugger ()->breakpoints_set_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_breakpoints_set_signal));

    debugger ()->breakpoints_list_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_breakpoints_list_signal));

    debugger ()->breakpoint_deleted_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_breakpoint_deleted_signal));

    debugger ()->got_overloads_choice_signal ().connect (sigc::mem_fun
        (*this, &DBGPerspective::on_debugger_got_overloads_choice_signal));

    debugger ()->stopped_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_stopped_signal));

    debugger ()->program_finished_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_program_finished_signal));

    debugger ()->engine_died_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_engine_died_signal));

    debugger ()->running_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_running_signal));

    debugger ()->signal_received_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_signal_received_by_target_signal));

    debugger ()->error_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_error_signal));

    debugger ()->state_changed_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_state_changed_signal));

    debugger ()->variable_value_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_variable_value_signal));

    debugger ()->got_target_info_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_debugger_got_target_info_signal));
}

void
DBGPerspective::clear_status_notebook (bool a_restarting)
{
    get_thread_list ().clear ();
    get_call_stack ().clear ();
    get_local_vars_inspector ().re_init_widget ();
    get_breakpoints_view ().clear ();
    get_registers_view ().clear ();
#ifdef WITH_MEMORYVIEW
    get_memory_view ().clear ();
#endif // WITH_MEMORYVIEW
    get_expr_monitor_view ().re_init_widget (a_restarting);
}

void
DBGPerspective::clear_session_data ()
{
    THROW_IF_FAIL (m_priv);

    m_priv->env_variables.clear ();
    m_priv->session_search_paths.clear ();
    delete_visual_breakpoints ();
    m_priv->global_search_paths.clear ();
}

void
DBGPerspective::append_source_editor (SourceEditor &a_sv,
                                      const UString &a_path)
{
    UString path = a_path;
    if (path.empty ()) {
        path = a_sv.get_path ();
        if (path.empty ())
            return;
    }

    if (m_priv->path_2_pagenum_map.find (path)
        != m_priv->path_2_pagenum_map.end ()) {
        THROW (UString ("File of '") + path + "' is already loaded");
    }

    UString basename = Glib::filename_to_utf8
        (Glib::path_get_basename (Glib::filename_from_utf8 (path)));

    SafePtr<Gtk::Label> label (Gtk::manage
                            (new Gtk::Label (basename)));
    label->set_ellipsize (Pango::ELLIPSIZE_MIDDLE);
    label->set_width_chars (basename.length ());
    label->set_max_width_chars (25);
    label->set_justify (Gtk::JUSTIFY_LEFT);
    SafePtr<Gtk::Image> cicon (manage
                (new Gtk::Image (Gtk::StockID (Gtk::Stock::CLOSE),
                                               Gtk::ICON_SIZE_MENU)));

    SafePtr<SlotedButton> close_button (Gtk::manage (new SlotedButton ()));
    //okay, make the button as small as possible.
    static const std::string button_style =
        "* {\n"
          "-GtkButton-default-border : 0;\n"
          "-GtkButton-default-outside-border : 0;\n"
          "-GtkButton-inner-border: 0;\n"
          "-GtkWidget-focus-line-width : 0;\n"
          "-GtkWidget-focus-padding : 0;\n"
          "padding: 0;\n"
        "}";
    Glib::RefPtr<Gtk::CssProvider> css = Gtk::CssProvider::create ();
    css->load_from_data (button_style);

    Glib::RefPtr<Gtk::StyleContext> context = close_button->get_style_context ();
    context->add_provider (css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    int w=0, h=0;
    Gtk::IconSize::lookup (Gtk::ICON_SIZE_MENU, w, h);
    close_button->set_size_request (w+2, h+2);

    close_button->perspective = this;
    close_button->set_relief (Gtk::RELIEF_NONE);
    close_button->set_focus_on_click (false);
    close_button->add (*cicon);
    close_button->file_path = path;
    close_button->signal_clicked ().connect
            (sigc::mem_fun (*close_button, &SlotedButton::on_clicked));
    UString message;
    message.printf (_("Close %s"), path.c_str ());
    close_button->set_tooltip_text (message);

    SafePtr<Gtk::HBox> hbox (Gtk::manage (new Gtk::HBox ()));
    // add a bit of space between the label and the close button
    hbox->set_spacing (4);

    Gtk::EventBox *event_box = Gtk::manage (new Gtk::EventBox);
    event_box->set_visible_window (false);
    event_box->add (*label);
    hbox->pack_start (*event_box);
    hbox->pack_start (*close_button, Gtk::PACK_SHRINK);
    event_box->set_tooltip_text (path);
    hbox->show_all ();
    a_sv.show_all ();
    int page_num = m_priv->sourceviews_notebook->insert_page (a_sv,
                                                              *hbox,
                                                              -1);
    m_priv->sourceviews_notebook->set_tab_reorderable (a_sv);

    std::string base_name =
                Glib::path_get_basename (Glib::filename_from_utf8 (path));
    THROW_IF_FAIL (base_name != "");
    m_priv->basename_2_pagenum_map[Glib::filename_to_utf8 (base_name)] =
                                                                    page_num;
    m_priv->path_2_pagenum_map[path] = page_num;
    m_priv->pagenum_2_source_editor_map[page_num] = &a_sv;
    m_priv->pagenum_2_path_map[page_num] = path;

    if (a_sv.get_buffer_type () == SourceEditor::BUFFER_TYPE_SOURCE)
        if (!do_monitor_file (path)) {
            LOG_ERROR ("Failed to start monitoring file: " << path);
        }

    hbox.release ();
    close_button.release ();
    label.release ();
    cicon.release ();

    if (a_sv.source_view ().get_has_window ()) {
        a_sv.source_view ().add_events (Gdk::BUTTON3_MOTION_MASK);

        a_sv.source_view ().signal_button_press_event ().connect
            (sigc::mem_fun
             (*this,
              &DBGPerspective::on_button_pressed_in_source_view_signal));

        // OK, this is a hack but it's the cleaner way I've found to
        // prevent the default popup menu of GtkTextView to show up
        // whent he user hits the "menu" keyboard key or shit F10.
        GTK_WIDGET_GET_CLASS (a_sv.source_view ().gobj())->popup_menu = NULL;

        // Then wire our own contextual popup menu when the user hits
        // the menu key.
       a_sv.source_view ().signal_popup_menu ().connect
            (sigc::mem_fun (*this,
                            &DBGPerspective::on_popup_menu));

        a_sv.source_view ().signal_motion_notify_event ().connect
            (sigc::mem_fun
             (*this,
              &DBGPerspective::on_motion_notify_event_signal));

        a_sv.source_view ().signal_leave_notify_event
                    ().connect_notify (sigc::mem_fun
                           (*this,
                            &DBGPerspective::on_leave_notify_event_signal));
    }

    if (get_num_notebook_pages () == 1) {
        m_priv->opened_file_action_group->set_sensitive (true);
        update_src_dependant_bp_actions_sensitiveness ();
        update_copy_action_sensitivity ();
    }
}

SourceEditor*
DBGPerspective::get_current_source_editor (bool a_load_if_nil)
{
    THROW_IF_FAIL (m_priv);

    if (!m_priv->sourceviews_notebook) {
        LOG_ERROR ("NULL m_priv->sourceviews_notebook");
        return 0;
    }

    if (a_load_if_nil
        && m_priv->sourceviews_notebook
        && !m_priv->sourceviews_notebook->get_n_pages ())
        // The source notebook is empty. If the current frame
        // has file info, load the file, bring it to the front,
        // apply decorations to it and return its editor.
        return get_source_editor_of_current_frame ();

    LOG_DD ("current pagenum: "
            << m_priv->current_page_num);

    map<int, SourceEditor*>::iterator iter, nil;
    nil = m_priv->pagenum_2_source_editor_map.end ();

    iter = m_priv->pagenum_2_source_editor_map.find
                                                (m_priv->current_page_num);
    if (iter == nil) {
        LOG_ERROR ("Could not find page num: "
                   << m_priv->current_page_num);
        return 0;
    }

    return iter->second;
}

/// Return the source editor of the current frame. If the current
/// frame doesn't have debug info then return 0. If we can't locate
/// (after trying very hard) the file of the current frame, return 0
/// too.
SourceEditor*
DBGPerspective::get_source_editor_of_current_frame (bool a_bring_to_front)
{
    if (m_priv->current_frame.has_empty_address ())
        return 0;

    UString path = m_priv->current_frame.file_full_name ();
    if (path.empty ())
        path = m_priv->current_frame.file_name ();
    if (path.empty ())
        return 0;
    if (!m_priv->find_file_or_ask_user (path, path,
                                        /*ignore_if_not_found=*/false))
        return 0;

    SourceEditor *editor = open_file_real (path);
    apply_decorations (editor,
                       /*scroll_to_where_marker=*/true);
    if (a_bring_to_front)
        bring_source_as_current (editor);

    return editor;
}

ISessMgr*
DBGPerspective::session_manager_ptr ()
{
    THROW_IF_FAIL (m_priv);

    if (!m_priv->session_manager) {
        m_priv->session_manager = ISessMgr::create (plugin_path ());
        THROW_IF_FAIL (m_priv->session_manager);
    }
    return m_priv->session_manager.get ();
}

UString
DBGPerspective::get_current_file_path ()
{
    SourceEditor *source_editor = get_current_source_editor ();
    if (!source_editor) {return "";}
    UString path;
    source_editor->get_path (path);
    return path;
}

SourceEditor*
DBGPerspective::get_source_editor_from_path (const UString &a_path,
                                             bool a_basename_only)
{
    UString actual_file_path;
    return get_source_editor_from_path (a_path,
                                        actual_file_path,
                                        a_basename_only);
}

bool
DBGPerspective::breakpoint_and_frame_have_same_file
                                    (const IDebugger::Breakpoint &a_bp,
                                     const IDebugger::Frame &a_frame) const
{
    if ((a_frame.file_full_name () == a_bp.file_full_name ()
         && !a_frame.file_full_name ().empty ())
        || (a_frame.file_name () == a_bp.file_name ()
            && !a_frame.file_name ().empty ()))
        return true;
    return false;

}

bool
DBGPerspective::get_frame_breakpoints_address_range
                                        (const IDebugger::Frame &a_frame,
                                         Range &a_range) const
{

    Range range = a_range;
    bool result = false;
    map<string, IDebugger::Breakpoint>::const_iterator it;
    for (it = m_priv->breakpoints.begin ();
         it != m_priv->breakpoints.end ();
         ++it) {
        if (breakpoint_and_frame_have_same_file (it->second, a_frame)) {
            range.extend (it->second.address ());
            result = true;
        }
    }
    if (result)
        a_range = range;
    return result;
}

/// Converts coordinates expressed in source view coordinates system into
/// coordinates expressed in the root window coordinate system.
/// \param a_x abscissa in source view coordinate system
/// \param a_y ordinate in source view coordinate system
/// \param a_root_x converted abscissa expressed in root window coordinate
///        system
/// \param a_root_y converted ordinate expressed in root window coordinate
///         system
/// \return true upon successful completion, false otherwise.
bool
DBGPerspective::source_view_to_root_window_coordinates (int a_x, int a_y,
                                                        int &a_root_x,
                                                        int &a_root_y)
{
    SourceEditor *editor = get_current_source_editor ();

    if (!editor)
        return false;

    Glib::RefPtr<Gdk::Window> gdk_window =
        ((Gtk::Widget&)editor->source_view ()).get_window ();

    THROW_IF_FAIL (gdk_window);

    int abs_x=0, abs_y=0;
    gdk_window->get_origin (abs_x, abs_y);
    a_root_x = a_x + abs_x;
    a_root_y = a_y + abs_y;

    return true;
}

/// For a given file path, find the file (open it in a source editor
/// if it's not opened yet) and return the source editor that contains
/// the contents of the file.
///
/// If the file could not be located, note that this function tries to
/// ask the user (via a dialog box) to locate it, and the user can
/// decide not to locate the file after all.
///
/// In the end if the file could not be located then a NULL pointer is
/// returned, so callers have to deal with that.
///
/// \param a_path the path to the source file to consider.
///
/// \param a_actual_file_path the resulting absolute path at which the
/// file was actually located.
///
/// \param whether to only consider the basename of the file in the
/// search.
///
/// \return a pointer to the resulting source editor or NULL if the
/// file could not be located.
SourceEditor*
DBGPerspective::get_source_editor_from_path (const UString &a_path,
                                             UString &a_actual_file_path,
                                             bool a_basename_only)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD

    LOG_DD ("a_path: " << a_path);
    LOG_DD ("a_basename_only" << (int) a_basename_only);

    if (a_path == "") {return 0;}

    map<UString, int>::iterator iter, nil;
    SourceEditor *result=0;

    if (a_basename_only) {
        std::string basename =
            Glib::path_get_basename (Glib::filename_from_utf8 (a_path));
        THROW_IF_FAIL (basename != "");
        iter = m_priv->basename_2_pagenum_map.find
                                        (Glib::filename_to_utf8 (basename));
        nil = m_priv->basename_2_pagenum_map.end ();
    } else {
        iter = m_priv->path_2_pagenum_map.find (a_path);
        nil = m_priv->path_2_pagenum_map.end ();
    }
    if (iter == nil) {
        return 0;
    }
    result = m_priv->pagenum_2_source_editor_map[iter->second];
    THROW_IF_FAIL (result);
    result->get_path (a_actual_file_path);
    return result;
}

SourceEditor*
DBGPerspective::get_or_append_source_editor_from_path (const UString &a_path)
{
    UString actual_file_path;

    if (a_path.empty ())
        return 0;

    SourceEditor *source_editor =
                    get_source_editor_from_path (a_path, actual_file_path);
    if (source_editor == 0) {
        if (!m_priv->find_file_or_ask_user (a_path, actual_file_path,
                                            /*ignore_if_not_found=*/false)) {
            return 0;
        }
        source_editor = open_file_real (actual_file_path);
    }
    return source_editor;
}

/// Try to get the "global" asm source editor. If not yet created,
/// create it.
/// \return the global asm source editor.
SourceEditor*
DBGPerspective::get_or_append_asm_source_editor ()
{
    UString path;
    SourceEditor *source_editor =
        get_source_editor_from_path (get_asm_title (), path);
    if (source_editor == 0) {
        Glib::RefPtr<Gsv::Buffer> source_buffer =
            SourceEditor::create_source_buffer ();
        source_editor =
            create_source_editor (source_buffer,
                                  /*a_asm_view=*/true,
                                  get_asm_title (),
                                  /*curren_line=*/-1,
                                  /*a_current_address=*/"");
        THROW_IF_FAIL (source_editor);
        append_source_editor (*source_editor, get_asm_title ());
    }
    THROW_IF_FAIL (source_editor);
    return source_editor;
}

IWorkbench&
DBGPerspective::workbench () const
{
    THROW_IF_FAIL (m_priv);
    THROW_IF_FAIL (m_priv->workbench);

    return *m_priv->workbench;
}

SourceEditor*
DBGPerspective::bring_source_as_current (const UString &a_path)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    LOG_DD ("file path: '" << a_path << "'");

    if (a_path.empty ())
        return 0;

    SourceEditor *source_editor = get_source_editor_from_path (a_path);
    if (!source_editor) {
        source_editor = open_file_real (a_path, -1, true);
        THROW_IF_FAIL (source_editor);
    }
    bring_source_as_current (source_editor);
    return source_editor;
}

void
DBGPerspective::bring_source_as_current (SourceEditor *a_editor)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (a_editor == 0)
        return;

    THROW_IF_FAIL (m_priv);

    UString path = a_editor->get_path ();
    map<UString, int>::iterator iter =
        m_priv->path_2_pagenum_map.find (path);
    THROW_IF_FAIL (iter != m_priv->path_2_pagenum_map.end ());
    m_priv->sourceviews_notebook->set_current_page (iter->second);
}

// Set the graphical "where pointer" to either the source (or assembly)
// location corresponding to a_frame.
// \a_frame the frame to consider
// \a_do_scroll if true, the source/asm editor is scrolled to the
// location of the newly set where pointer.
// \return true upon successful completion, false otherwise.
bool
DBGPerspective::set_where (const IDebugger::Frame &a_frame,
                           bool a_do_scroll, bool a_try_hard)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    UString file_path = a_frame.file_full_name ();
    if (file_path.empty ())
        file_path = a_frame.file_name ();

    SourceEditor *editor = 0;
    if (!file_path.empty ())
        editor = get_or_append_source_editor_from_path (file_path);
    if (!editor)
        editor = get_or_append_asm_source_editor ();

    RETURN_VAL_IF_FAIL (editor, false);

    SourceEditor::BufferType type = editor->get_buffer_type ();
    switch (type) {
        case SourceEditor::BUFFER_TYPE_SOURCE:
            return set_where (editor, a_frame.line (), a_do_scroll);
        case SourceEditor::BUFFER_TYPE_ASSEMBLY:
            return set_where (editor, a_frame.address (),
                              a_do_scroll, a_try_hard);
        case SourceEditor::BUFFER_TYPE_UNDEFINED:
            break;
    }
    return false;
}

// Set the graphical "where pointer" to the source line a_line.
// \param a_path the path (uri) of the current source file.
// \a_line the line number (starting from 1) to which the "where pointer"
// is to be set.
// \a_do_scroll if true, the source editor is scrolled to the
// location of the newly set where pointer.
// \return true upon successful completion, false otherwise.
bool
DBGPerspective::set_where (const UString &a_path,
                           int a_line,
                           bool a_do_scroll)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    SourceEditor *source_editor = bring_source_as_current (a_path);
    return set_where (source_editor, a_line, a_do_scroll);
}

// Set the graphical "where pointer" to the source line a_line.
// \param a_editor the source editor that contains the source to
// consider.
// \a_line the line number (starting from 1) to which the "where pointer"
// is to be set.
// \a_do_scroll if true, the source editor is scrolled to the
// location of the newly set where pointer.
// \return true upon successful completion, false otherwise.
bool
DBGPerspective::set_where (SourceEditor *a_editor,
                           int a_line,
                           bool a_do_scroll)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (!a_editor)
        return false;

    THROW_IF_FAIL (a_editor->get_buffer_type ()
                   == SourceEditor::BUFFER_TYPE_SOURCE);

    bring_source_as_current (a_editor);

    a_editor->move_where_marker_to_line (a_line, a_do_scroll);
    Gtk::TextBuffer::iterator iter =
        a_editor->source_view ().get_buffer ()->get_iter_at_line (a_line - 1);
    if (!iter) {
        return false;
    }
    a_editor->source_view().get_buffer ()->place_cursor (iter);
    return true;
}

// Set the graphical "where pointer" to the assembly address a_address.
// \param a_editor the source editor that contains the source to
// consider.
// \a_address the assembly address to which the "where pointer"
// is to be set.
// \a_do_scroll if true, the source editor is scrolled to the
// location of the newly set where pointer.
// \return true upon successful completion, false otherwise.
bool
DBGPerspective::set_where (SourceEditor *a_editor,
                           const Address &a_address,
                           bool a_do_scroll,
                           bool a_try_hard,
                           bool a_approximate)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (!a_editor)
        return false;

    THROW_IF_FAIL (a_editor->get_buffer_type ()
                   == SourceEditor::BUFFER_TYPE_ASSEMBLY);

    bring_source_as_current (a_editor);

    // The IP points to the *next* instruction to execute. What we want
    // is the current instruction executed. So lets get the line of the
    // address that comes right before a_address.
    if (!a_editor->move_where_marker_to_address (a_address, a_do_scroll,
                                                 a_approximate)) {
        if (a_try_hard) {
            pump_asm_including_address (a_editor, a_address);
            return true;
        } else {
            LOG_ERROR ("Fail to get line for address: "
                       << a_address.to_string ());
            return false;
        }
    }
    a_editor->place_cursor_at_address (a_address);
    return true;
}

void
DBGPerspective::unset_where ()
{
    map<int, SourceEditor*>::iterator iter;
    for (iter = m_priv->pagenum_2_source_editor_map.begin ();
         iter !=m_priv->pagenum_2_source_editor_map.end ();
         ++iter) {
        if (!(iter->second)) {continue;}
        iter->second->unset_where_marker ();
    }
}

Gtk::Widget*
DBGPerspective::get_contextual_menu ()
{
    THROW_IF_FAIL (m_priv && m_priv->contextual_menu_merge_id);

    if (m_priv->contextual_menu == NULL) {
        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "CopyMenuItem",
             "CopyMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui_separator
            (m_priv->contextual_menu_merge_id, "/ContextualMenu");

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "InspectExpressionMenuItem",
             "InspectExpressionMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui_separator
            (m_priv->contextual_menu_merge_id, "/ContextualMenu");

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "ToggleBreakpointMenuItem",
             "ToggleBreakpointMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "ToggleEnableBreakpointMenuItem",
             "ToggleEnableBreakpointMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "ToggleCountpointMenuItem",
             "ToggleCountpointMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "NextMenuItem",
             "NextMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "StepMenuItem",
             "StepMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "StepOutMenuItem",
             "StepOutMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "ContinueMenuItem",
             "ContinueMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "ContinueUntilMenuItem",
             "ContinueUntilMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "JumpToCurrentLocationMenuItem",
             "JumpToCurrentLocationMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "JumpAndBreakToCurrentLocationMenuItem",
             "JumpAndBreakToCurrentLocationMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "StopMenuItem",
             "StopMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "RunMenuItem",
             "RunMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui_separator
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu");

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "FindMenutItem",
             "FindMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "ReloadSourceMenutItem",
             "ReloadSourceMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->add_ui
            (m_priv->contextual_menu_merge_id,
             "/ContextualMenu",
             "RefreshLocalVariablesMenuItem",
             "RefreshLocalVariablesMenuItemAction",
             Gtk::UI_MANAGER_AUTO,
             false);

        workbench ().get_ui_manager ()->ensure_update ();
        m_priv->contextual_menu =
            workbench ().get_ui_manager ()->get_widget
            ("/ContextualMenu");
    }

    THROW_IF_FAIL (m_priv->contextual_menu);

    return m_priv->contextual_menu;
}

/// Get the contextual menu, massage it as necessary and pop it up.
void
DBGPerspective::setup_and_popup_contextual_menu ()
{
    GdkEventButton *event = m_priv->source_view_event_button;
    RETURN_IF_FAIL (event);

    SourceEditor *editor = get_current_source_editor ();
    THROW_IF_FAIL (editor);

    editor->setup_and_popup_menu
        (event, NULL,
         dynamic_cast<Gtk::Menu*> (get_contextual_menu ()));
}

bool
DBGPerspective::uses_launch_terminal () const
{
    THROW_IF_FAIL (m_priv);
    return m_priv->use_launch_terminal;
}

void
DBGPerspective::uses_launch_terminal (bool a_flag)
{
    THROW_IF_FAIL (m_priv);
    m_priv->use_launch_terminal = a_flag;
}


ThreadList&
DBGPerspective::get_thread_list ()
{
    THROW_IF_FAIL (m_priv);
    THROW_IF_FAIL (debugger ());
    if (!m_priv->thread_list) {
        m_priv->thread_list.reset  (new ThreadList (debugger ()));
    }
    THROW_IF_FAIL (m_priv->thread_list);
    return *m_priv->thread_list;
}

list<UString>&
DBGPerspective::get_global_search_paths ()
{

    THROW_IF_FAIL (m_priv);

    if (m_priv->global_search_paths.empty ()) {
        read_default_config ();
    }
    return m_priv->global_search_paths;
}

bool
DBGPerspective::do_monitor_file (const UString &a_path)
{
    THROW_IF_FAIL (m_priv);

    if (m_priv->path_2_monitor_map.find (a_path) !=
        m_priv->path_2_monitor_map.end ()) {
        return false;
    }
    Glib::RefPtr<Gio::FileMonitor> monitor;
    Glib::RefPtr<Gio::File> file = Gio::File::create_for_path (a_path);
    THROW_IF_FAIL (file);
    monitor = file->monitor_file ();
    THROW_IF_FAIL (monitor);
    monitor->signal_changed (). connect (sigc::bind (sigc::ptr_fun
                (gio_file_monitor_cb), this));
    m_priv->path_2_monitor_map[a_path] = monitor;

    LOG_DD ("Monitoring file '" << Glib::filename_from_utf8 (a_path));
    return true;
}

bool
DBGPerspective::do_unmonitor_file (const UString &a_path)
{
    THROW_IF_FAIL (m_priv);

    // Disassembly result is composite content that doesn't come from
    // any on-disk file. It's thus not monitored.
    if (m_priv->is_asm_title (a_path))
        return true;

    Priv::Path2MonitorMap::iterator it =
                            m_priv->path_2_monitor_map.find (a_path);

    if (it == m_priv->path_2_monitor_map.end ()) {
        return false;
    }
    if (it->second) {
        it->second->cancel ();
    }
    m_priv->path_2_monitor_map.erase (it);
    return true;
}

void
DBGPerspective::read_default_config ()
{
    THROW_IF_FAIL (m_priv->workbench);
    IConfMgr &conf_mgr = get_conf_mgr ();
    if (m_priv->global_search_paths.empty ()) {
        UString dirs;

        NEMIVER_TRY;

        conf_mgr.get_key_value (CONF_KEY_NEMIVER_SOURCE_DIRS, dirs);

        NEMIVER_CATCH_NOX;

        LOG_DD ("got source dirs '" << dirs << "' from conf mgr");
        if (!dirs.empty ()) {
            m_priv->global_search_paths = dirs.split_to_list (":");
            LOG_DD ("that makes '" <<(int)m_priv->global_search_paths.size()
                    << "' dir paths");
        }

        NEMIVER_TRY;

        conf_mgr.get_key_value (CONF_KEY_SHOW_DBG_ERROR_DIALOGS,
                                m_priv->show_dbg_errors);
        NEMIVER_CATCH_NOX;

        conf_mgr.value_changed_signal ().connect
            (sigc::mem_fun (*this,
                            &DBGPerspective::on_conf_key_changed_signal));
    }

    NEMIVER_TRY;

    conf_mgr.get_key_value (CONF_KEY_HIGHLIGHT_SOURCE_CODE,
                            m_priv->enable_syntax_highlight);
    conf_mgr.get_key_value (CONF_KEY_SHOW_SOURCE_LINE_NUMBERS,
                            m_priv->show_line_numbers);
    conf_mgr.get_key_value (CONF_KEY_CONFIRM_BEFORE_RELOAD_SOURCE,
                            m_priv->confirm_before_reload_source);
    conf_mgr.get_key_value (CONF_KEY_USE_SYSTEM_FONT,
                            m_priv->use_system_font);
    conf_mgr.get_key_value (CONF_KEY_CUSTOM_FONT_NAME,
                            m_priv->custom_font_name);
    conf_mgr.get_key_value (CONF_KEY_SYSTEM_FONT_NAME,
                            m_priv->system_font_name,
                            CONF_NAMESPACE_DESKTOP_INTERFACE);
    conf_mgr.get_key_value (CONF_KEY_USE_LAUNCH_TERMINAL,
                            m_priv->use_launch_terminal);
    conf_mgr.get_key_value (CONF_KEY_DEFAULT_NUM_ASM_INSTRS,
                            m_priv->num_instr_to_disassemble);
    // m_priv->num_instr_to_disassemble should never be 0.  If it is,
    // then something is wrong with the local configuration manager
    // setup.
    if (m_priv->num_instr_to_disassemble == 0)
        m_priv->num_instr_to_disassemble = NUM_INSTR_TO_DISASSEMBLE;
    conf_mgr.get_key_value (CONF_KEY_ASM_STYLE_PURE,
                            m_priv->asm_style_pure);
    conf_mgr.get_key_value (CONF_KEY_PRETTY_PRINTING,
                            m_priv->enable_pretty_printing);
    NEMIVER_CATCH_NOX;

    UString style_id ("classic");

    NEMIVER_TRY
    conf_mgr.get_key_value (CONF_KEY_EDITOR_STYLE_SCHEME, style_id);
    NEMIVER_CATCH_NOX

    m_priv->editor_style = Gsv::StyleSchemeManager::get_default
        ()->get_scheme (style_id);

    default_config_read_signal ().emit ();
}

int
DBGPerspective::get_num_notebook_pages ()
{
    THROW_IF_FAIL (m_priv && m_priv->sourceviews_notebook);

    return m_priv->sourceviews_notebook->get_n_pages ();
}

void
DBGPerspective::record_and_save_new_session ()
{
    THROW_IF_FAIL (m_priv);
    if (m_priv->prog_path.empty ()) {
        // Don't save emtpy sessions.
        return;
    }
    ISessMgr::Session session;
    record_and_save_session (session);
}

IProcMgr*
DBGPerspective::get_process_manager ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->process_manager) {
        m_priv->process_manager = IProcMgr::create ();
        THROW_IF_FAIL (m_priv->process_manager);
    }
    return m_priv->process_manager.get ();
}


/// Get the "word" located around position (a_x,a_y), consider it as a
/// variable name, request the debugging engine for the content of that
/// variable and display that variable in a popup tip.
/// If the debugger engine doesn't respond, that certainly means the word
/// was not a variable name.
/// \param a_x the abscissa of the position to consider
/// \param a_y the ordinate of the position to consider
void
DBGPerspective::try_to_request_show_variable_value_at_position (int a_x,
                                                                int a_y)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD
    SourceEditor *editor = get_current_source_editor ();
    THROW_IF_FAIL (editor);

    UString var_name;
    Gdk::Rectangle start_rect, end_rect;
    if (!get_current_source_editor ()->get_word_at_position (a_x, a_y,
                                                             var_name,
                                                             start_rect,
                                                             end_rect)) {
        return;
    }

    if (var_name == "") {return;}

    int abs_x=0, abs_y=0;
    if (!source_view_to_root_window_coordinates (a_x, a_y, abs_x, abs_y))
        return;
    m_priv->in_show_var_value_at_pos_transaction = true;
    m_priv->var_popup_tip_x = abs_x;
    m_priv->var_popup_tip_y = abs_y;
    m_priv->var_to_popup = var_name;
    debugger ()->create_variable
        (var_name,
         sigc::mem_fun (*this,
                        &DBGPerspective::on_variable_created_for_tooltip_signal));
}

/// Popup a tip at a given position, showing some text content.
/// \param a_x the absissa to consider
/// \param a_y the ordinate to consider
/// \param a_text the text to show
void
DBGPerspective::show_underline_tip_at_position (int a_x,
                                                int a_y,
                                                const UString &a_text)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD
    LOG_DD ("showing text in popup: '"
            << Glib::locale_from_utf8 (a_text)
            << "'");
    get_popup_tip ().text (a_text);
    get_popup_tip ().show_at_position (a_x, a_y);
}

/// Popup a tip at a given position, showing some the content of a variable.
/// \param a_x the absissa to consider
/// \param a_y the ordinate to consider
/// \param a_text the text to show
void
DBGPerspective::show_underline_tip_at_position
                                        (int a_x, int a_y,
                                         const IDebugger::VariableSafePtr a_var)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD
    get_popup_tip ().show_at_position (a_x, a_y);
    get_popup_expr_inspector ().set_expression (a_var,
                                               true/*expand variable*/,
                                               m_priv->pretty_printing_toggled);
}

ExprInspector&
DBGPerspective::get_popup_expr_inspector ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD

    if (!m_priv->popup_expr_inspector)
        m_priv->popup_expr_inspector.reset
                    (new ExprInspector (*debugger (),
                                       *const_cast<DBGPerspective*> (this)));
    THROW_IF_FAIL (m_priv->popup_expr_inspector);
    return *m_priv->popup_expr_inspector;
}

void
DBGPerspective::restart_mouse_immobile_timer ()
{
    LOG_FUNCTION_SCOPE_NORMAL_D (DBG_PERSPECTIVE_MOUSE_MOTION_DOMAIN);

    THROW_IF_FAIL (m_priv);
    THROW_IF_FAIL (m_priv->workbench);

    m_priv->timeout_source_connection.disconnect ();
    m_priv->timeout_source_connection =
        Glib::signal_timeout ().connect_seconds
            (sigc::mem_fun
                 (*this, &DBGPerspective::on_mouse_immobile_timer_signal),
             1);
}

void
DBGPerspective::stop_mouse_immobile_timer ()
{
    LOG_FUNCTION_SCOPE_NORMAL_D (DBG_PERSPECTIVE_MOUSE_MOTION_DOMAIN);
    THROW_IF_FAIL (m_priv);
    m_priv->timeout_source_connection.disconnect ();
}

/// Get the Popup tip object. Create it, if necessary. Reuse it when
/// possible.
/// \return the popup tip.
PopupTip&
DBGPerspective::get_popup_tip ()
{
    THROW_IF_FAIL (m_priv);

    if (!m_priv->popup_tip) {
        m_priv->popup_tip.reset (new PopupTip);
        Gtk::ScrolledWindow *w = Gtk::manage (new PopupScrolledWindow ());
        w->set_policy (Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
        m_priv->popup_tip->set_child (*w);
        w->add (get_popup_expr_inspector ().widget ());
        m_priv->popup_tip->signal_hide ().connect (sigc::mem_fun
                   (*this, &DBGPerspective::on_popup_tip_hide));
    }
    THROW_IF_FAIL (m_priv->popup_tip);
    return *m_priv->popup_tip;
}

/// Hide the variable popup tip if the mouse pointer is outside of the window
/// of said variable popup tip. Do nothing otherwise.
/// \param x the abscissa of the mouse pointer relative to the root window
/// \param y the ordinate of the mouse pointer relative to the root window
void
DBGPerspective::hide_popup_tip_if_mouse_is_outside (int x, int y)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (!m_priv->popup_tip || !m_priv->popup_tip->get_window ())
        return;

    int popup_orig_x = 0, popup_orig_y = 0;
    m_priv->popup_tip->get_window ()->get_origin (popup_orig_x,
                                                  popup_orig_y);
    int popup_border = m_priv->popup_tip->get_border_width ();
    Gdk::Rectangle alloc =
        m_priv->popup_tip->get_allocation ();
    alloc.set_x (popup_orig_x);
    alloc.set_y (popup_orig_y);

    LOG_DD ("mouse (x,y): (" << x << "," << y << ")");
    LOG_DD ("alloc (x,y,w,h): ("
            << alloc.get_x ()      << ","
            << alloc.get_y ()      << ","
            << alloc.get_width ()  << ","
            << alloc.get_height () << ")");
    if (x > alloc.get_x () + alloc.get_width () + popup_border
        || x + popup_border + 2 < alloc.get_x ()
        || y > alloc.get_y () + alloc.get_height () + popup_border
        || y + popup_border + 2 < alloc.get_y ()) {
        LOG_DD ("hidding popup tip");
        m_priv->popup_tip->hide ();
    }

}

FindTextDialog&
DBGPerspective::get_find_text_dialog ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->find_text_dialog) {
        m_priv->find_text_dialog.reset (new FindTextDialog (plugin_path ()));
        m_priv->find_text_dialog->signal_response ().connect
            (sigc::mem_fun (*this,
                            &DBGPerspective::on_find_text_response_signal));
    }
    THROW_IF_FAIL (m_priv->find_text_dialog);

    return *m_priv->find_text_dialog;
}

/// Adds the views of the debugger to the layout.  It adds them in a
/// left to right manner (if you consider the defaut layout at
/// least).
void
DBGPerspective::add_views_to_layout ()
{
    THROW_IF_FAIL (m_priv);

    m_priv->layout ().append_view (get_terminal_box (),
                                   TARGET_TERMINAL_VIEW_TITLE,
                                   TARGET_TERMINAL_VIEW_INDEX);
    m_priv->layout ().append_view (get_context_paned (),
                                   CONTEXT_VIEW_TITLE,
                                   CONTEXT_VIEW_INDEX);
    m_priv->layout ().append_view (get_breakpoints_scrolled_win (),
                                   BREAKPOINTS_VIEW_TITLE,
                                   BREAKPOINTS_VIEW_INDEX);
    m_priv->layout ().append_view (get_registers_scrolled_win (),
                                   REGISTERS_VIEW_TITLE,
                                   REGISTERS_VIEW_INDEX);
    #ifdef WITH_MEMORYVIEW
    m_priv->layout ().append_view (get_memory_view ().widget (),
                                   MEMORY_VIEW_TITLE,
                                   MEMORY_VIEW_INDEX);
    #endif // WITH_MEMORYVIEW
    m_priv->layout ().append_view (get_expr_monitor_view ().widget (),
                                   EXPR_MONITOR_VIEW_TITLE,
                                   EXPR_MONITOR_VIEW_INDEX);
    m_priv->layout ().do_init ();

}

void
DBGPerspective::record_and_save_session (ISessMgr::Session &a_session)
{
    THROW_IF_FAIL (m_priv);
    if (m_priv->prog_path.empty ()) {
        // Don't save empty sessions.
        return;
    }
    UString session_name =
        Glib::filename_to_utf8 (Glib::path_get_basename
                                (Glib::filename_from_utf8 (m_priv->prog_path)));

    if (session_name == "") {return;}

    UString caption_session_name;
    if (a_session.properties ().count (CAPTION_SESSION_NAME)) {
        caption_session_name = a_session.properties ()[CAPTION_SESSION_NAME];
    }

    if (a_session.session_id ()) {
        session_manager ().clear_session (a_session.session_id ());
        LOG_DD ("cleared current session: "
                << (int) m_priv->session.session_id ());
    }

    UString today;
    dateutils::get_current_datetime (today);
    session_name += "-" + today;
    if (caption_session_name.empty ()) {
        caption_session_name = session_name;
    }
    UString prog_args = UString::join (m_priv->prog_args,
                                       PROG_ARG_SEPARATOR);
    a_session.properties ().clear ();
    a_session.properties ()[SESSION_NAME] = session_name;
    a_session.properties ()[PROGRAM_NAME] = m_priv->prog_path;
    a_session.properties ()[PROGRAM_ARGS] = prog_args;
    a_session.properties ()[PROGRAM_CWD] = m_priv->prog_cwd;
    a_session.properties ()[REMOTE_TARGET] = m_priv->remote_target;
    a_session.properties ()[SOLIB_PREFIX] = m_priv->solib_prefix;
    a_session.properties ()[CAPTION_SESSION_NAME] = caption_session_name;

    GTimeVal timeval;
    g_get_current_time (&timeval);
    UString time;
    a_session.properties ()[LAST_RUN_TIME] =
                                time.printf ("%ld", timeval.tv_sec);
    a_session.env_variables () = m_priv->env_variables;

    a_session.opened_files ().clear ();
    map<UString, int>::const_iterator path_iter;
    for (path_iter = m_priv->path_2_pagenum_map.begin ();
         path_iter != m_priv->path_2_pagenum_map.end ();
         ++path_iter) {
        // Avoid saving non persistent files, e.g., things like
        // disassembly buffers named "<Disassembly>"
        if (m_priv->is_persistent_file (path_iter->first))
            a_session.opened_files ().push_back (path_iter->first);
    }

    // Record regular breakpoints and watchpoints in the session
    a_session.breakpoints ().clear ();
    a_session.watchpoints ().clear ();
    map<string, IDebugger::Breakpoint>::const_iterator break_iter;
    map<string, bool> parent_ids_added;
    map<string, bool>::const_iterator end = parent_ids_added.end ();

    for (break_iter = m_priv->breakpoints.begin ();
         break_iter != m_priv->breakpoints.end ();
         ++break_iter) {
        if ((break_iter->second.type ()
             == IDebugger::Breakpoint::STANDARD_BREAKPOINT_TYPE)
            || (break_iter->second.type ()
             == IDebugger::Breakpoint::COUNTPOINT_TYPE)) {
            UString parent_id = break_iter->second.parent_id ();
            if (parent_ids_added.find (parent_id) != end)
                continue;
            ISessMgr::Breakpoint bp (break_iter->second.file_name (),
                                     break_iter->second.file_full_name (),
                                     break_iter->second.line (),
                                     break_iter->second.enabled (),
                                     break_iter->second.condition (),
                                     break_iter->second.initial_ignore_count (),
                                     debugger ()->is_countpoint
                                     (break_iter->second));
            a_session.breakpoints ().push_back (bp);
            parent_ids_added[parent_id] = true;
            LOG_DD ("Regular breakpoint scheduled to be stored");
        } else if (break_iter->second.type ()
                   == IDebugger::Breakpoint::WATCHPOINT_TYPE) {
            ISessMgr::WatchPoint wp (break_iter->second.expression (),
                                     break_iter->second.is_write_watchpoint (),
                                     break_iter->second.is_read_watchpoint ());
            a_session.watchpoints ().push_back (wp);
            LOG_DD ("Watchpoint scheduled to be stored");
        }
    }

    THROW_IF_FAIL (session_manager_ptr ());

    a_session.search_paths ().clear ();
    list<UString>::const_iterator search_path_iter;
    for (search_path_iter = m_priv->session_search_paths.begin ();
         search_path_iter != m_priv->session_search_paths.end ();
         ++search_path_iter) {
        a_session.search_paths ().push_back (*search_path_iter);
    }
    THROW_IF_FAIL (session_manager_ptr ());

    //erase all sessions but the 5 last ones, otherwise, the number
    //of debugging session stored will explode with time.
    std::list<ISessMgr::Session> sessions =
        session_manager_ptr ()->sessions ();
    int nb_sessions = sessions.size ();
    if (nb_sessions > 5) {
        int nb_sessions_to_erase = sessions.size () - 5;
        std::list<ISessMgr::Session>::const_iterator it;
        for (int i=0; i < nb_sessions_to_erase; ++i) {
            THROW_IF_FAIL (sessions.begin () != sessions.end ());
            session_manager_ptr ()->delete_session
                (sessions.begin ()->session_id ());
            sessions.erase (sessions.begin ());
        }
    }

    //now store the current session
    session_manager_ptr ()->store_session
        (a_session,
         session_manager_ptr ()->default_transaction ());
}


//*******************
//</private methods>
//*******************

DBGPerspective::DBGPerspective (DynamicModule *a_dynmod) :
    IDBGPerspective (a_dynmod),
    m_priv (new Priv)
{
}

void
DBGPerspective::do_init (IWorkbench *a_workbench)
{
    THROW_IF_FAIL (m_priv);
    m_priv->workbench = a_workbench;
    register_layouts ();
    init_icon_factory ();
    init_actions ();
    init_toolbar ();
    init_body ();
    init_signals ();
    init_debugger_signals ();
    read_default_config ();
    session_manager ().load_sessions
                        (session_manager ().default_transaction ());
    workbench ().shutting_down_signal ().connect (sigc::mem_fun
            (*this, &DBGPerspective::on_shutdown_signal));
    m_priv->initialized = true;
}

DBGPerspective::~DBGPerspective ()
{
    LOG_D ("deleted", "destructor-domain");
}

const UString&
DBGPerspective::get_perspective_identifier ()
{
    static UString s_id = "org.nemiver.DebuggerPerspective";
    return s_id;
}

void
DBGPerspective::register_layouts ()
{
    THROW_IF_FAIL (m_priv);

    m_priv->layout_mgr.register_layout
        (LayoutSafePtr (new DBGPerspectiveDefaultLayout));
    m_priv->layout_mgr.register_layout
        (LayoutSafePtr (new DBGPerspectiveTwoPaneLayout));
    m_priv->layout_mgr.register_layout
        (LayoutSafePtr (new DBGPerspectiveWideLayout));
#ifdef WITH_DYNAMICLAYOUT
    m_priv->layout_mgr.register_layout
        (LayoutSafePtr (new DBGPerspectiveDynamicLayout));
#endif // WITH_DYNAMICLAYOUT
}

void
DBGPerspective::get_toolbars (list<Gtk::Widget*>  &a_tbs)
{
    CHECK_P_INIT;
    a_tbs.push_back (m_priv->toolbar.get ());
}

Gtk::Widget*
DBGPerspective::get_body ()
{
    CHECK_P_INIT;
    return m_priv->layout ().widget ();
}

Gtk::Widget&
DBGPerspective::get_source_view_widget ()
{
    return *m_priv->sourceviews_notebook;
}

IWorkbench&
DBGPerspective::get_workbench ()
{
    CHECK_P_INIT;
    return workbench ();
}

void
DBGPerspective::edit_workbench_menu ()
{
    CHECK_P_INIT;

    add_perspective_menu_entries ();
}

SourceEditor*
DBGPerspective::create_source_editor (Glib::RefPtr<Gsv::Buffer> &a_source_buf,
                                      bool a_asm_view,
                                      const UString &a_path,
                                      int a_current_line,
                                      const UString &a_current_address)
{
    NEMIVER_TRY

    SourceEditor *source_editor;
    Gtk::TextIter cur_line_iter;
    int current_line =  -1;

    if (a_asm_view) {
        source_editor = Gtk::manage (new SourceEditor (plugin_path (),
                                                       a_source_buf,
                                                       true));
        if (!a_current_address.empty ()) {
            source_editor->assembly_buf_addr_to_line
                                (Address (a_current_address.raw ()),
                                 /*approximate=*/false,
                                 current_line);
        }
    } else {
        source_editor = Gtk::manage (new SourceEditor (plugin_path (),
                                                       a_source_buf,
                                                       false));
        source_editor->source_view ().set_show_line_numbers
                                                (m_priv->show_line_numbers);
        current_line = a_current_line;
    }

    if (current_line > 0) {
        Gtk::TextIter cur_line_iter =
                a_source_buf->get_iter_at_line (current_line);
        if (cur_line_iter) {
            Glib::RefPtr<Mark> where_marker =
                a_source_buf->create_source_mark (WHERE_MARK,
                                                  WHERE_CATEGORY,
                                                  cur_line_iter);
            THROW_IF_FAIL (where_marker);
        }
    }

    // detect when the user clicks on the editor
    // so we can know when the cursor position changes
    // and we can enable / disable actions that are valid
    // for only certain lines
    source_editor->insertion_changed_signal ().connect
        (sigc::bind (sigc::mem_fun
                         (*this,
                          &DBGPerspective::on_insertion_changed_signal),
                          source_editor));

    if (!m_priv->get_source_font_name ().empty ()) {
        Pango::FontDescription font_desc (m_priv->get_source_font_name ());
        source_editor->source_view ().override_font (font_desc);
    }
    if (m_priv->get_editor_style ()) {
        source_editor->source_view ().get_source_buffer ()->set_style_scheme
            (m_priv->get_editor_style ());
    }
    source_editor->set_path (a_path);
    source_editor->marker_region_got_clicked_signal ().connect
    (sigc::bind
        (sigc::mem_fun (*this,
                        &DBGPerspective::on_sv_markers_region_clicked_signal),
         source_editor));

    m_priv->opened_file_action_group->set_sensitive (true);

    return source_editor;

    NEMIVER_CATCH_AND_RETURN (0)
}

void
DBGPerspective::open_file ()
{
    OpenFileDialog dialog (plugin_path (),
                           debugger (),
                           get_current_file_path ());

    //file_chooser.set_current_folder (m_priv->prog_cwd);

    int result = dialog.run ();

    if (result != Gtk::RESPONSE_OK) {return;}

    vector<string> paths;
    dialog.get_filenames (paths);
    vector<string>::const_iterator iter;
    for (iter = paths.begin (); iter != paths.end (); ++iter) {
        open_file_real (*iter, -1, true);
    }
    bring_source_as_current (*(paths.begin()));
}

bool
DBGPerspective::open_file (const UString &a_path, int current_line)
{
    return open_file_real (a_path, current_line) ? true: false;
}

SourceEditor*
DBGPerspective::open_file_real (const UString &a_path,
                                int a_current_line)
{
    RETURN_VAL_IF_FAIL (m_priv, 0);
    if (a_path.empty ())
        return 0;

    SourceEditor *source_editor = 0;
    if ((source_editor = get_source_editor_from_path (a_path)))
        return source_editor;

    NEMIVER_TRY

    Glib::RefPtr<Gsv::Buffer> source_buffer;
    if (!m_priv->load_file (a_path, source_buffer))
        return 0;

    source_editor = create_source_editor (source_buffer,
                                          /*a_asm_view=*/false,
                                          a_path,
                                          a_current_line,
                                          /*a_current_address=*/"");

    THROW_IF_FAIL (source_editor);
    append_source_editor (*source_editor, a_path);

    NEMIVER_CATCH_AND_RETURN (0)
    return source_editor;
}

SourceEditor*
DBGPerspective::open_file_real (const UString &a_path,
                                int current_line,
                                bool a_reload_visual_breakpoint)
{
    THROW_IF_FAIL (m_priv);

    SourceEditor *editor = open_file_real (a_path, current_line);
    if (editor && a_reload_visual_breakpoint) {
        apply_decorations (editor);
    }
    return editor;
}

void
DBGPerspective::close_current_file ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    if (!get_num_notebook_pages ()) {return;}

    // We need to copy the path and pass it to close_file() because if we pass
    // it the reference to the map value, we will get corruption because
    // close_file() modifies the map
    UString path = m_priv->pagenum_2_path_map[m_priv->current_page_num];
    close_file (path);
}

void
DBGPerspective::find_in_current_file ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    get_find_text_dialog ().show ();
}

void
DBGPerspective::close_file (const UString &a_path)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    LOG_DD ("removing file: " << a_path);
    map<UString, int>::const_iterator nil, iter;
    nil = m_priv->path_2_pagenum_map.end ();
    iter = m_priv->path_2_pagenum_map.find (a_path);
    if (iter == nil) {
        LOG_DD ("could not find page " << a_path);
        return;
    }

    int page_num = m_priv->path_2_pagenum_map[a_path];
    LOG_DD ("removing notebook tab number "
            << (int) (page_num)
            << ", path " << a_path);
    m_priv->sourceviews_notebook->remove_page (page_num);
    m_priv->current_page_num =
        m_priv->sourceviews_notebook->get_current_page ();

    if (!do_unmonitor_file (a_path)) {
        LOG_ERROR ("failed to unmonitor file " << a_path);
    }

    if (!get_num_notebook_pages ()) {
        m_priv->opened_file_action_group->set_sensitive (false);
        update_src_dependant_bp_actions_sensitiveness ();
    }
    update_file_maps ();
}

const char*
DBGPerspective::get_asm_title ()
{
    return DISASSEMBLY_TITLE;
}

bool
DBGPerspective::load_asm (const common::DisassembleInfo &a_info,
                          const std::list<common::Asm> &a_asm,
                          Glib::RefPtr<Gsv::Buffer> &a_source_buffer)
{
    list<UString> where_to_look_for_src;
    m_priv->build_find_file_search_path (where_to_look_for_src);
    return SourceEditor::load_asm (a_info, a_asm, /*a_append=*/true,
                                   where_to_look_for_src,
                                   m_priv->session_search_paths,
                                   m_priv->paths_to_ignore,
                                   a_source_buffer);
}

// If no asm dedicated tab was already present in the perspective,
// create  a new one, otherwise reuse the one that was already present.
// Then load the assembly insns a_asm  described by a_info into the
// source buffer of the asm tab.
// Return true upon successful completion, false otherwise.
SourceEditor*
DBGPerspective::open_asm (const common::DisassembleInfo &a_info,
                          const std::list<common::Asm> &a_asm,
                          bool a_set_where)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    SourceEditor *source_editor = 0;
    NEMIVER_TRY

    Glib::RefPtr<Gsv::Buffer> source_buffer;

    source_editor = get_source_editor_from_path (get_asm_title ());

    if (source_editor) {
        source_buffer = source_editor->source_view ().get_source_buffer ();
        source_buffer->erase (source_buffer->begin (), source_buffer->end ());
    }

    if (!load_asm (a_info, a_asm, source_buffer))
        return 0;

    if (!source_editor)
        source_editor =
            get_or_append_asm_source_editor ();

    NEMIVER_CATCH_AND_RETURN (0);

    if (source_editor && a_set_where) {
        if (!m_priv->current_frame.has_empty_address ())
            set_where (source_editor, m_priv->current_frame.address (),
                       /*do_scroll=*/true, /*try_hard=*/true);
    }

    return source_editor;
}

// Get the source editor of the source file being currently debugged,
// switch it into the asm mode and load the asm insns
// represented by a_info and a_asm into its source buffer.
// \param a_info descriptor of the assembly instructions
// \param a_asm a list of asm instructions.
void
DBGPerspective::switch_to_asm (const common::DisassembleInfo &a_info,
                               const std::list<common::Asm> &a_asm)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    SourceEditor *source_editor = get_source_editor_of_current_frame ();
    switch_to_asm (a_info, a_asm, source_editor);
}

/// Switch a_source_editor into the asm mode and load the asm insns
/// represented by a_info and a_asm into its source buffer.
/// \param a_info descriptor of the assembly instructions
/// \param a_asm a list of asm instructions.
/// \param a_source_editor the source editor to switch into asm mode.
/// \param a_approximate_where if true and if the current instruction
/// pointer's address is missing from the list of asm instructions,
/// try set the where marker to the closest address that comes right
/// after the instruction pointer's address. Otherwise, try to
/// disassemble and pump in more asm instructions whose addresses will
/// likely contain the one of the instruction pointer.
void
DBGPerspective::switch_to_asm (const common::DisassembleInfo &a_info,
                               const std::list<common::Asm> &a_asm,
                               SourceEditor *a_source_editor,
                               bool a_approximate_where)
{
    if (!a_source_editor)
        return;

    a_source_editor->clear_decorations ();

    Glib::RefPtr<Gsv::Buffer> asm_buf;
    if ((asm_buf = a_source_editor->get_assembly_source_buffer ()) == 0) {
        SourceEditor::setup_buffer_mime_and_lang (asm_buf, "text/x-asm");
        a_source_editor->register_assembly_source_buffer (asm_buf);
        asm_buf = a_source_editor->get_assembly_source_buffer ();
        RETURN_IF_FAIL (asm_buf);
    }
    if (!load_asm (a_info, a_asm, asm_buf)) {
        LOG_ERROR ("failed to load asm");
        return;
    }
    if (!a_source_editor->switch_to_assembly_source_buffer ()) {
        LOG_ERROR ("Could not switch the current view to asm");
        return;
    }
    a_source_editor->current_line (-1);
    apply_decorations_to_asm (a_source_editor,
                              /*scroll_to_where_marker=*/true,
                              a_approximate_where);
}

void
DBGPerspective::pump_asm_including_address (SourceEditor *a_editor,
                                            const Address &a_address)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    IDebugger::DisassSlot slot =
        sigc::bind (sigc::mem_fun (this,
                                   &DBGPerspective::on_debugger_asm_signal2),
                    a_editor);

    disassemble_around_address_and_do (a_address, slot);
}

// Get the source editor of the source file being currently debugged,
// switch it into the source code mode. If necessary, (re) load the
// source code.
void
DBGPerspective::switch_to_source_code ()
{
    SourceEditor *source_editor = get_source_editor_of_current_frame ();
    if (source_editor == 0)
        return;

    source_editor->clear_decorations ();

    Glib::RefPtr<Gsv::Buffer> source_buf;
    UString source_path;
    if ((source_buf = source_editor->get_non_assembly_source_buffer ()) == 0) {
        // Woops!
        // We don't have any source code buffer. Let's try hard to get
        // the source code corresponding to the current frame. For that,
        // we'll hope to have proper debug info for the binary being
        // debugged, and the source code available on disk.
        if (m_priv->current_frame.has_empty_address ()) {
            LOG_DD ("No current instruction pointer");
            return;
        }
        if (m_priv->current_frame.file_name ().empty ()) {
            LOG_DD ("No file name information for current frame");
            return;
        }
        UString absolute_path, mime_type;
        if (!m_priv->find_file_or_ask_user (m_priv->current_frame.file_name (),
                                            absolute_path,
                                            /*ignore_if_not_found=*/false)) {
            LOG_DD ("Could not find file: "
                    << m_priv->current_frame.file_name ());
            return;
        }
        SourceEditor::get_file_mime_type (absolute_path, mime_type);
        SourceEditor::setup_buffer_mime_and_lang (source_buf, mime_type);
        m_priv->load_file (absolute_path, source_buf);
        source_editor->register_non_assembly_source_buffer (source_buf);
    }
    source_editor->switch_to_non_assembly_source_buffer ();
    apply_decorations (source_editor,
                       /*scroll_to_where_marker=*/true);
}

Gtk::Widget*
DBGPerspective::load_menu (const UString &a_filename,
                           const UString &a_widget_name)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    Gtk::Widget *result =
        workbench ().get_ui_manager ()->get_widget (a_widget_name);

    if (!result) {
        string relative_path = Glib::build_filename ("menus", a_filename);
        string absolute_path;
        THROW_IF_FAIL (build_absolute_resource_path
                            (Glib::filename_to_utf8 (relative_path),
                             absolute_path));
        workbench ().get_ui_manager ()->add_ui_from_file
            (Glib::filename_to_utf8 (absolute_path));

        result = workbench ().get_ui_manager ()->get_widget (a_widget_name);
    }

    return result;
}

void
DBGPerspective::close_opened_files ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (!get_num_notebook_pages ()) {return;}

    map<UString, int>::iterator it;
    //loop until all the files are closed or until
    //we did 50 iterations. This prevents us against
    //infinite loops
    for (int i=0; i < 50; ++i) {
        it = m_priv->path_2_pagenum_map.begin ();
        if (it == m_priv->path_2_pagenum_map.end ()) {
            break;
        }
        LOG_DD ("closing page " << it->first);
        UString path = it->first;
        close_file (path);
    }
}

/// Walks the list of source files opened
/// in the Notebook and rebuilds the different maps
/// that we use to speed up access.
/// This should be used when a notebook tab is closed.
void
DBGPerspective::update_file_maps ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    THROW_IF_FAIL (m_priv);

    m_priv->path_2_pagenum_map.clear ();
    m_priv->basename_2_pagenum_map.clear ();
    m_priv->pagenum_2_source_editor_map.clear ();
    m_priv->pagenum_2_path_map.clear ();

    SourceEditor *se = 0;
    UString path, basename;
    int nb_pages = m_priv->sourceviews_notebook->get_n_pages ();

    for (int i = 0; i < nb_pages; ++i) {
        se = dynamic_cast<SourceEditor*>
            (m_priv->sourceviews_notebook->get_nth_page (i));
        THROW_IF_FAIL (se);
        se->get_path (path);
        basename = Glib::filename_to_utf8 (Glib::path_get_basename
                                           (Glib::filename_from_utf8 (path)));
        m_priv->path_2_pagenum_map[path] = i;
        m_priv->basename_2_pagenum_map[basename] = i;
        m_priv->pagenum_2_source_editor_map[i] = se;
        m_priv->pagenum_2_path_map[i] = path;
    }
}

bool
DBGPerspective::reload_file (const UString &a_path)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    SourceEditor *editor = get_source_editor_from_path (a_path);

    if (!editor)
        return open_file (a_path);

    Glib::RefPtr<Gsv::Buffer> buffer =
        editor->source_view ().get_source_buffer ();
    int current_line = editor->current_line ();
    int current_column = editor->current_column ();

    if (!m_priv->load_file (a_path, buffer))
        return false;
    editor->register_non_assembly_source_buffer (buffer);
    editor->current_line (current_line);
    editor->current_column (current_column);
    apply_decorations (a_path);
    return true;
}

bool
DBGPerspective::reload_file ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    SourceEditor *editor = get_current_source_editor ();
    if (!editor)
        return false;
    UString path;
    editor->get_path (path);
    if (path.empty ())
        return false;
    LOG_DD ("going to reload file path: "
            << Glib::filename_from_utf8 (path));
    reload_file (path);
    return true;
}

ISessMgr&
DBGPerspective::session_manager ()
{
    return *session_manager_ptr ();
}

void
DBGPerspective::execute_session (ISessMgr::Session &a_session)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    save_current_session ();
    m_priv->session = a_session;

    if (a_session.properties ()[PROGRAM_CWD] != m_priv->prog_path
        && get_num_notebook_pages ()) {
        close_opened_files ();
    }

    m_priv->prog_cwd = a_session.properties ()[PROGRAM_CWD];

    IDebugger::Breakpoint breakpoint;
    vector<IDebugger::Breakpoint> breakpoints;
    for (list<ISessMgr::Breakpoint>::const_iterator it =
             m_priv->session.breakpoints ().begin ();
         it != m_priv->session.breakpoints ().end ();
         ++it) {
        breakpoint.clear ();
        breakpoint.line (it->line_number ());
        breakpoint.file_name (it->file_name ());
        breakpoint.file_full_name (it->file_full_name ());
        breakpoint.enabled (it->enabled ());
        breakpoint.condition (it->condition ());
        breakpoint.initial_ignore_count (it->ignore_count ());
        if (it->is_countpoint ()) {
            breakpoint.type (IDebugger::Breakpoint::COUNTPOINT_TYPE);
            LOG_DD ("breakpoint "
                    << it->file_name () << ":" << it->line_number ()
                    << " is a countpoint");
        }
        breakpoints.push_back (breakpoint);
    }

    for (list<ISessMgr::WatchPoint>::const_iterator it =
           m_priv->session.watchpoints ().begin ();
         it != m_priv->session.watchpoints ().end ();
         ++it) {
        breakpoint.clear ();
        breakpoint.type (IDebugger::Breakpoint::WATCHPOINT_TYPE);
        breakpoint.expression (it->expression ());
        breakpoint.is_read_watchpoint (it->is_read ());
        breakpoint.is_write_watchpoint (it->is_write ());
        breakpoints.push_back (breakpoint);
    }

    // populate the list of search paths from the current session
    list<UString>::const_iterator path_iter;
    m_priv->session_search_paths.clear();
    for (path_iter = m_priv->session.search_paths ().begin ();
            path_iter != m_priv->session.search_paths ().end ();
            ++path_iter) {
        m_priv->session_search_paths.push_back (*path_iter);
    }

    // open the previously opened files
    for (path_iter = m_priv->session.opened_files ().begin ();
            path_iter != m_priv->session.opened_files ().end ();
            ++path_iter) {
        open_file(*path_iter);
    }

    vector<UString> args =
        a_session.properties ()[PROGRAM_ARGS].split (PROG_ARG_SEPARATOR);

    map<UString, UString>::const_iterator it,
        nil = a_session.properties ().end ();

    UString prog_name = a_session.properties ()[PROGRAM_NAME];

    UString remote_target, solib_prefix;
    if ((it = a_session.properties ().find (REMOTE_TARGET)) != nil)
        remote_target = it->second;
    if (!remote_target.empty ())
        if ((it = a_session.properties ().find (SOLIB_PREFIX)) != nil)
            solib_prefix = it->second;

    if (!remote_target.empty ())
        reconnect_to_remote_target (remote_target, prog_name, solib_prefix);
    else
        execute_program (prog_name,
                         args,
                         a_session.env_variables (),
                         a_session.properties ()[PROGRAM_CWD],
                         breakpoints);
    m_priv->reused_session = true;
}

void
DBGPerspective::execute_program ()
{
    RunProgramDialog dialog (plugin_path ());

    // set defaults from session
    if (debugger ()->get_target_path () != "") {
        dialog.program_name (debugger ()->get_target_path ());
    }
    dialog.arguments (UString::join (m_priv->prog_args,
                                     " "));
    if (m_priv->prog_cwd == "") {
        m_priv->prog_cwd = Glib::filename_to_utf8 (Glib::get_current_dir ());
    }
    dialog.working_directory (m_priv->prog_cwd);
    dialog.environment_variables (m_priv->env_variables);

    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        return;
    }

    vector<UString> args;
    UString prog, cwd;
    prog = dialog.program_name ();
    THROW_IF_FAIL (prog != "");
    args = dialog.arguments ().split (" ");
    cwd = dialog.working_directory ();
    THROW_IF_FAIL (cwd != "");
    map<UString, UString> env = dialog.environment_variables();

    vector<IDebugger::Breakpoint> breaks;
    execute_program (prog, args, env, cwd, breaks,
                     /*a_restarting=*/true,
                     /*a_close_opened_files=*/true);
    m_priv->reused_session = false;
}

/// Re starts the last program that was being previously debugged.
/// If the underlying debugger engine died, this function will restart it,
/// reload the program that was being debugged,
/// and set all the breakpoints that were set previously.
/// This is useful when e.g. the debugger engine died and we want to
/// restart it and restart the program that was being debugged.
void
DBGPerspective::restart_inferior ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (!is_connected_to_remote_target ()) {
        // Restarting a local program
        restart_local_inferior ();
    } else {
        // We cannot restart an inferior running on a remote target at
        // the moment.
        ui_utils::display_error (_("Sorry, it's impossible to restart "
                                   "a remote inferior"));
    }
}

/// Restart the execution of an inferior, only if we got attached to
/// it locally (i.e, not remotely).  This preserves breakpoints that
/// are set.  If we are currently attached to the inferior, then the
/// function just re-runs it. The advantage of doing this is that the
/// debugging engine will not re-read its init file (and possibly
/// execute stuff from it like setting breakpoints). Re reading the
/// init file and executing what is possibly there can lead to make
/// restarting become slower and slower.  But we are not currently
/// attached to the inferior, well, then the function has no choice
/// but re-loading the binary of the inferior and start it. This makes
/// the debugging engine re-read its init file, though.
void
DBGPerspective::restart_local_inferior ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    THROW_IF_FAIL (!is_connected_to_remote_target ());

    if (// If the program the user asked is a libtool wrapper shell
        // script, then the real program we are debugging is at a
        // different path from the (libtool shell script) path
        // requested by the user.  So we cannot just ask the debugger
        // to re-run the binary it's current running, as libtool can
        // have changed changed the path to the underlying real
        // binary.  This can happen, e.g, when the user re-compiled
        // the program she is debugging and then asks for a re-start
        // of the inferior in Nemiver.  In that case, libtool might
        // have changed the real binary path name.
        !is_libtool_executable_wrapper(m_priv->last_prog_path_requested)
        // If we are not attached to the target, there is not debugger
        // engine to ask a re-run to ...
        && debugger ()->is_attached_to_target ()
        // Make sure we are restarting the program we were running
        // right before. We need to make sure because the user can
        // have changed the path to the inferior and ask for a
        // restart; in which case, we can't just simply call debugger
        // ()->run ().
        && debugger ()->get_target_path () == m_priv->prog_path) {
        going_to_run_target_signal ().emit (true);
        debugger ()->re_run
            (sigc::mem_fun
             (*this, &DBGPerspective::on_debugger_inferior_re_run_signal));
    } else {
        vector<IDebugger::Breakpoint> bps;
        execute_program (m_priv->last_prog_path_requested, m_priv->prog_args,
                         m_priv->env_variables, m_priv->prog_cwd,
                         bps,
                         true /* be aware we are restarting the same inferior*/,
                         false /* don't close opened files */);
    }
}

/// Execute a program for the first time.  Do not use this to restart
/// an existing program; rather, use restart_inferior for that.
void
DBGPerspective::execute_program (const UString &a_prog,
                                 const vector<UString> &a_args,
                                 const map<UString, UString> &a_env,
                                 const UString &a_cwd,
                                 bool a_close_opened_files,
                                 bool a_break_in_main_run)
{
    vector<IDebugger::Breakpoint> bps;
    execute_program (a_prog, a_args, a_env,
                     a_cwd, bps, /*a_restarting=*/false,
                     a_close_opened_files,
                     a_break_in_main_run);
}

/// Loads and executes a program (called an inferior) under the debugger.
/// This function can also set breakpoints before running the inferior.
///
/// \param a_prog the path to the program to debug
///
/// \param a_args the arguments of the program to debug
///
/// \param a_env the environment variables to set prior to running the
///  inferior. Those environment variables will be accessible to the
///  inferior.
///
/// \param a_cwd the working directory in which to run the inferior
///
/// \param a_breaks the breakpoints to set prior to running the inferior.
///
/// \param a_close_opened_files if true, close the files that have been
///        opened in the debugging perspective.
///
/// \param a_restarting if true, be kind if the program to run has be
///  run previously. Be kind means things like do not re do things
///  that have been done already, e.g. re set breakpoints etc.
///  Otherwise, just ignore the fact that the program might have been
///  run previously and just redo all the necessary things.
///
/// \param a_close_opened_files if true, close all the opened files
///  before executing the inferior.
///
/// \param a_break_in_main_run if true, set a breakpoint in the "main"
///  function of the inferior and, if the breakpoint is actually set
///  there, then run it.  The inferior will then run and stop at the
///  beginning of the main function.
void
DBGPerspective::execute_program
                        (const UString &a_prog,
                         const vector<UString> &a_args,
                         const map<UString, UString> &a_env,
                         const UString &a_cwd,
                         const vector<IDebugger::Breakpoint> &a_breaks,
                         bool a_restarting,
                         bool a_close_opened_files,
                         bool a_break_in_main_run)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD

    NEMIVER_TRY

    THROW_IF_FAIL (m_priv);

    IDebuggerSafePtr dbg_engine = debugger ();
    THROW_IF_FAIL (dbg_engine);

    LOG_DD ("debugger state: '"
            << IDebugger::state_to_string (dbg_engine->get_state ())
            << "'");

    UString prog = a_prog;
    // First of all, make sure the program we want to debug exists.
    // Note that Nemiver can be invoked like 'nemiver fooprog'
    // or 'nemiver /absolute/path/to/fooprog'.
    // In the former form, nemiver will look for the program
    // in the $PATH environment variable and use the resulting absolute
    // path.
    // In the later form, nemiver will just use the absolute path.
    if (!Glib::file_test (Glib::filename_from_utf8 (prog),
                          Glib::FILE_TEST_IS_REGULAR)) {
        // We didn't find prog. If the path to prog is not absolute,
        // look it up in the directories pointed to by the
        // $PATH environment variable.
        if (Glib::path_is_absolute (Glib::filename_from_utf8 (prog))
            || !env::build_path_to_executable (prog, prog)) {
            UString msg;
            msg.printf (_("Could not find file %s"), prog.c_str ());
            ui_utils::display_error (msg);
            return;
        }
    }

    // if the engine is running, stop it.
    if (dbg_engine->get_state () == IDebugger::RUNNING) {
        dbg_engine->stop_target ();
        LOG_DD ("stopped dbg_engine");
    }

    // close old files that might be open
    if (a_close_opened_files
        && (prog != m_priv->prog_path)
        && get_num_notebook_pages ()) {
        close_opened_files ();
    }

    vector<UString> source_search_dirs = a_cwd.split (" ");

    // Detect if we are debugging a new program or not.
    // For instance, if we are debugging the same program as in
    // the previous run, but with different arguments, we consider
    // that we are not debugging a new program.
    // In that case, we might want to keep things like breakpoints etc,
    // around.
    bool is_new_program = a_restarting
        ? (prog != m_priv->last_prog_path_requested)
        : true;
    LOG_DD ("is new prog: " << is_new_program);

    // Save the current breakpoints aside.
    map<string, IDebugger::Breakpoint> saved_bps = m_priv->breakpoints;

    // delete old breakpoints, if any.
    m_priv->breakpoints.clear();
    map<string, IDebugger::Breakpoint>::const_iterator bp_it;
    for (bp_it = saved_bps.begin ();
         bp_it != saved_bps.end ();
         ++bp_it) {
        if (m_priv->debugger_engine_alive)
            dbg_engine->delete_breakpoint (bp_it->first);
    }

    if (is_new_program) {
        // If we are debugging a new program,
        // clear data gathered by the old session
        clear_session_data ();
    }

    LOG_DD ("load program");

    // now really load the inferior program (i.e: the one to be
    // debugged)

    if (dbg_engine->load_program (prog, a_args, a_cwd,
                                  source_search_dirs,
                                  get_terminal_name (),
                                  uses_launch_terminal (),
                                  get_terminal ().slave_pty (),
                                  a_restarting) == false) {
        UString message;
        message.printf (_("Could not load program: %s"),
                        prog.c_str ());
        display_error (message);
        return;
    }

    m_priv->debugger_engine_alive = true;

    // set environment variables of the inferior
    dbg_engine->add_env_variables (a_env);

    // If this is a new program we are debugging,
    // set a breakpoint in 'main' by default.
    if (a_breaks.empty ()) {
        LOG_DD ("here");
        if (!is_new_program) {
            LOG_DD ("here");
            map<string, IDebugger::Breakpoint>::const_iterator it;
            map<string, bool> bps_set;
            UString parent_id;
            for (it = saved_bps.begin ();
                 it != saved_bps.end ();
                 ++it) {
                // for breakpoints that are sub-breakpoints of a parent
                // breakpoint, only set the parent breakpoint once.
                if (it->second.has_multiple_locations ()) {
                    for (vector<IDebugger::Breakpoint>::const_iterator i =
                             it->second.sub_breakpoints ().begin();
                         i != it->second.sub_breakpoints ().end ();
                         ++i) {
                        parent_id = i->parent_id ();
                        if (bps_set.find (parent_id) != bps_set.end ())
                            continue;
                        set_breakpoint (*i);
                        bps_set[parent_id] = true;
                    }
                } else {
                    parent_id = it->second.parent_id();
                    if (bps_set.find (parent_id) != bps_set.end ())
                        continue;
                    set_breakpoint (it->second);
                    bps_set[parent_id] = true;
                }
            }
            if (!saved_bps.empty())
                // We are restarting the same program, and we hope that
                // some that at least one breakpoint is actually going to
                // be set.  So let's schedule the continuation of the
                // inferior's execution.
                run_real (a_restarting);
        } else if (a_break_in_main_run) {
            LOG_DD ("here");
            dbg_engine->set_breakpoint
                ("main",
                 sigc::bind
                 (sigc::mem_fun
                  (*this,
                   &DBGPerspective::on_debugger_bp_automatically_set_on_main),
                  a_restarting));
        }
    } else {
        LOG_DD ("here");
        vector<IDebugger::Breakpoint>::const_iterator it;
        map<string, bool> bps_set;
        UString parent_id;
        for (it = a_breaks.begin (); it != a_breaks.end (); ++it) {
            parent_id = it->parent_id ();
            if (bps_set.find (parent_id) != bps_set.end ())
                continue;
            set_breakpoint (*it);
            bps_set[parent_id] = true;
        }
        // Here we are starting (or restarting) the program and we
        // hope at least one breakpoint is going to be set; so lets
        // schedule the continuation of the execution of the inferior.
        run_real (a_restarting);
    }

    m_priv->last_prog_path_requested = prog;
    m_priv->prog_path = prog;
    m_priv->prog_args = a_args;
    m_priv->prog_cwd = a_cwd;
    m_priv->env_variables = a_env;

    NEMIVER_CATCH
}

void
DBGPerspective::attach_to_program ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD

    IProcMgr *process_manager = get_process_manager ();
    THROW_IF_FAIL (process_manager);
    ProcListDialog dialog (plugin_path (),
                           *process_manager);
    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        return;
    }
    if (dialog.has_selected_process ()) {
        IProcMgr::Process process;
        THROW_IF_FAIL (dialog.get_selected_process (process));
        attach_to_program (process.pid ());
    }
}

void
DBGPerspective::attach_to_program (unsigned int a_pid,
                                   bool a_close_opened_files)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    LOG_DD ("a_pid: " << (int) a_pid);

    if (a_pid == (unsigned int) getpid ()) {
        ui_utils::display_warning (_("You cannot attach to Nemiver itself"));
        return;
    }

    save_current_session ();

    if (a_close_opened_files && get_num_notebook_pages ()) {
        close_opened_files ();
    }

    if (!debugger ()->attach_to_target (a_pid,
                                        get_terminal_name ())) {

        ui_utils::display_warning (_("You cannot attach to the "
                                     "underlying debugger engine"));
        return;
    }
}

void
DBGPerspective::connect_to_remote_target ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    RemoteTargetDialog dialog (plugin_path ());

    // try to pre-fill the remote target dialog with the relevant info
    // if we have it.
    pre_fill_remote_target_dialog (dialog);
    
    int result = dialog.run ();

    if (result != Gtk::RESPONSE_OK)
        return;

    UString path = dialog.get_executable_path ();
    LOG_DD ("executable path: '" <<  path << "'");
    UString solib_prefix = dialog.get_solib_prefix_path ();

    if (dialog.get_connection_type ()
        == RemoteTargetDialog::TCP_CONNECTION_TYPE) {
        connect_to_remote_target (dialog.get_server_address (),
                                  dialog.get_server_port (),
                                  path, solib_prefix);
    } else if (dialog.get_connection_type ()
               == RemoteTargetDialog::SERIAL_CONNECTION_TYPE) {
        connect_to_remote_target (dialog.get_serial_port_name (),
                                  path, solib_prefix);
    }
}

void
DBGPerspective::connect_to_remote_target (const UString &a_server_address,
                                          unsigned a_server_port,
                                          const UString &a_prog_path,
                                          const UString &a_solib_prefix)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD
    THROW_IF_FAIL (debugger ());

    save_current_session ();

    if (m_priv->prog_cwd.empty ())
        m_priv->prog_cwd = Glib::filename_to_utf8 (Glib::get_current_dir ());

    LOG_DD ("executable path: '" <<  a_prog_path << "'");
    vector<UString> args;

    if (debugger ()->load_program (a_prog_path , args,
                                   m_priv->prog_cwd) == false) {
        UString message;
        message.printf (_("Could not load program: %s"),
                        a_prog_path.c_str ());
        display_error (message);
        return;
    }
    LOG_DD ("solib prefix path: '" <<  a_solib_prefix << "'");
    debugger ()->set_solib_prefix_path (a_solib_prefix);
    debugger ()->attach_to_remote_target (a_server_address,
                                          a_server_port);
    std::ostringstream remote_target;
    remote_target << a_server_address << ":" << a_server_port;
    m_priv->remote_target = remote_target.str ();
    m_priv->prog_path = a_prog_path;
    m_priv->solib_prefix = a_solib_prefix;
}

void
DBGPerspective::connect_to_remote_target (const UString &a_serial_line,
                                          const UString &a_prog_path,
                                          const UString &a_solib_prefix)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD
    THROW_IF_FAIL (debugger ());

    save_current_session ();

    if (m_priv->prog_cwd.empty ())
        m_priv->prog_cwd = Glib::filename_to_utf8 (Glib::get_current_dir ());

    LOG_DD ("executable path: '" <<  a_prog_path << "'");

    vector<UString> args;
    if (debugger ()->load_program (a_prog_path , args,
                                   m_priv->prog_cwd) == false) {
        UString message;
        message.printf (_("Could not load program: %s"),
                        a_prog_path.c_str ());
        display_error (message);
        return;
    }
    LOG_DD ("solib prefix path: '" <<  a_solib_prefix << "'");
    debugger ()->set_solib_prefix_path (a_solib_prefix);
    debugger ()->attach_to_remote_target (a_serial_line);

    std::ostringstream remote_target;
    remote_target << a_serial_line;
    m_priv->remote_target = remote_target.str ();
    m_priv->solib_prefix = a_solib_prefix;
    m_priv->prog_path = a_prog_path;
}

void
DBGPerspective:: connect_to_remote_target (const UString &a_command_line,
                                          const UString &a_prog_path,
                                          const UString &a_solib_prefix)

{
    LOG_FUNCTION_SCOPE_NORMAL_DD
    THROW_IF_FAIL (debugger ());

    save_current_session ();

    if (m_priv->prog_cwd.empty ())
        m_priv->prog_cwd = Glib::filename_to_utf8 (Glib::get_current_dir ());

    LOG_DD ("executable path: '" <<  a_prog_path << "'");

    vector<UString> args;
    if (debugger ()->load_program (a_prog_path , args,
                                   m_priv->prog_cwd) == false) {
        UString message;
        message.printf (_("Could not load program: %s"),
                        a_prog_path.c_str ());
        display_error (message);
        return;
    }
	LOG_DD ("solib prefix path: '" <<  a_solib_prefix << "'");
    debugger ()->set_solib_prefix_path (a_solib_prefix);
    debugger ()->attach_to_remote_target (a_command_line);

    std::ostringstream remote_target;
    remote_target << a_command_line;
    m_priv->remote_target = remote_target.str ();
    m_priv->solib_prefix = a_solib_prefix;
    m_priv->prog_path = a_prog_path;
}


void
DBGPerspective::reconnect_to_remote_target (const UString &a_remote_target,
                                            const UString &a_prog_path,
                                            const UString &a_solib_prefix)
{
    if (a_remote_target.empty ())
        return;

    string host;
    unsigned port;
    if (str_utils::parse_host_and_port (a_remote_target, host, port))
        // Try to connect via IP
        connect_to_remote_target (host, port,
                                  a_prog_path,
                                  a_solib_prefix);
    else
        // Try to connect via the serial line
        connect_to_remote_target (a_remote_target,
                                  a_prog_path,
                                  a_solib_prefix);    
}

bool
DBGPerspective::is_connected_to_remote_target ()
{
    return (debugger ()->is_attached_to_target ()
            && !m_priv->remote_target.empty ());
}

void
DBGPerspective::pre_fill_remote_target_dialog (RemoteTargetDialog &a_dialog)
{
    THROW_IF_FAIL (m_priv);

    if (m_priv->remote_target.empty ()
        || m_priv->prog_path.empty ())
        return;

    RemoteTargetDialog::ConnectionType connection_type;

    string host;
    unsigned port;
    if (str_utils::parse_host_and_port (m_priv->remote_target,
                                        host, port))
        connection_type = RemoteTargetDialog::TCP_CONNECTION_TYPE;
    else
        connection_type = RemoteTargetDialog::SERIAL_CONNECTION_TYPE;

    a_dialog.set_cwd (m_priv->prog_cwd);
    a_dialog.set_solib_prefix_path (m_priv->solib_prefix);
    a_dialog.set_executable_path (m_priv->prog_path);
    a_dialog.set_connection_type (connection_type);
    if (connection_type == RemoteTargetDialog::TCP_CONNECTION_TYPE) {
        a_dialog.set_server_address (host);
        a_dialog.set_server_port (port);
    } else {
        a_dialog.set_serial_port_name (m_priv->remote_target);
    }
}

void
DBGPerspective::detach_from_program ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD
    THROW_IF_FAIL (debugger ());

    if (!debugger ()->is_attached_to_target ())
        return;

    save_current_session ();

    if (is_connected_to_remote_target ())
        debugger ()->disconnect_from_remote_target ();
    else
        debugger ()->detach_from_target ();
}

void
DBGPerspective::save_current_session ()
{
    if (m_priv->reused_session) {
        record_and_save_session (m_priv->session);
        LOG_DD ("saved current session");
    } else {
        LOG_DD ("recorded a new session");
        record_and_save_new_session ();
    }
}

void
DBGPerspective::choose_a_saved_session ()
{
    SavedSessionsDialog dialog (plugin_path (), session_manager_ptr ());
    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        return;
    }
    ISessMgr::Session session = dialog.session ();
    execute_session (session);
}

void
DBGPerspective::edit_preferences ()
{
    THROW_IF_FAIL (m_priv);
    PreferencesDialog dialog (*this, m_priv->layout_mgr, plugin_path ());
    dialog.run ();
}

/// Run the program being debugged (a.k.a. the inferior) from the start.
/// If the program was running already, restart it.
/// If the underlying debugging engine died, restart it, and re run the
/// the inferior.
void
DBGPerspective::run ()
{
    THROW_IF_FAIL (m_priv);

    LOG_DD ("debugger engine not alive. "
            "Checking if it should be restarted ...");

    if (!m_priv->prog_path.empty ()) {
        LOG_DD ("Yes, it seems we were running a program before. "
                "Will try to restart it");
        restart_inferior ();
    } else if (m_priv->debugger_engine_alive) {
        run_real (/*a_restarting=*/false);
    } else {
        LOG_ERROR ("No program got previously loaded");
    }
}

/// Really run the inferior (invoking IDebugger).  This is sub-routine
/// of the run and execute_program methods.
void
DBGPerspective::run_real (bool a_restarting)
{
    going_to_run_target_signal ().emit (a_restarting);
    debugger ()->run ();
    m_priv->debugger_has_just_run = true;
}

void
DBGPerspective::load_core_file ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    LoadCoreDialog dialog (plugin_path ());

    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        return;
    }

    UString prog_path, core_path;
    prog_path = dialog.program_name ();
    THROW_IF_FAIL (prog_path != "");
    core_path = dialog.core_file ();
    THROW_IF_FAIL (core_path != "");

    load_core_file (prog_path, core_path);
}

void
DBGPerspective::load_core_file (const UString &a_prog_path,
                                const UString &a_core_file_path)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    THROW_IF_FAIL (m_priv);

    if (a_prog_path != m_priv->prog_path && get_num_notebook_pages ()) {
        close_opened_files ();
    }

    debugger ()->load_core_file (a_prog_path, a_core_file_path);
    get_call_stack ().update_stack (/*select_top_most=*/true);
}

void
DBGPerspective::stop ()
{
    LOG_FUNCTION_SCOPE_NORMAL_D (NMV_DEFAULT_DOMAIN);
    if (!debugger ()->stop_target ()) {
        ui_utils::display_error (_("Failed to stop the debugger"));
    }
}

void
DBGPerspective::step_over ()
{
    debugger ()->step_over ();
}

void
DBGPerspective::step_into ()
{
    debugger ()->step_in ();
}

void
DBGPerspective::step_out ()
{
    debugger ()->step_out ();
}

void
DBGPerspective::step_in_asm ()
{
    debugger ()->step_in_asm ();
}

void
DBGPerspective::step_over_asm ()
{
    debugger ()->step_over_asm ();
}

// Start the inferior if it has not started yet, or make it continue
// its execution.
void
DBGPerspective::do_continue ()
{
    if (!debugger ()->is_running ())
        debugger ()->run ();
    else
        debugger ()->do_continue ();
}

void
DBGPerspective::do_continue_until ()
{
    SourceEditor *editor = get_current_source_editor ();
    THROW_IF_FAIL (editor);

    UString file_path;
    editor->get_file_name (file_path);
    int current_line = editor->current_line ();

    debugger ()->continue_to_position (file_path, current_line);
}

/// Jump (transfer execution of the inferior) to the location selected
/// by the user in the source editor.
void
DBGPerspective::do_jump_to_current_location ()
{
    SourceEditor *editor = get_current_source_editor ();
    THROW_IF_FAIL (editor);

    int current_line = editor->current_line ();
    UString file_path;
    editor->get_file_name (file_path);
    SourceLoc loc (file_path, current_line);
    debugger ()->jump_to_position (loc, &null_default_slot);
}

///  Set a breakpoint to a given location and jump (transfer execution
///  of the inferior) there.
///
///  \param a_location the location to set breakpoint and jump to
void
DBGPerspective::do_jump_and_break_to_location (const Loc &a_location)
{
#define JUMP_TO_LOC_AFTER_ENABLE_BP(LOC)                    \
    debugger ()->enable_breakpoint                          \
        (bp->id (),                                     \
         sigc::bind                                         \
         (sigc::mem_fun                                     \
          (*this,                                           \
           &DBGPerspective::jump_to_location),              \
          (LOC)));

#define JUMP_TO_LOC_AFTER_SET_BP(LOC)                   \
    debugger ()->set_breakpoint                         \
        (a_location,                                    \
         /*a_condition=*/"",/*a_ignore_count=*/0,       \
         sigc::bind                                     \
         (sigc::mem_fun                                 \
          (*this,                                       \
           &DBGPerspective::on_break_before_jump),      \
          (LOC)));

    bool bp_enabled = false;
    if (is_breakpoint_set_at_location (a_location,
                                       bp_enabled)) {
        if (bp_enabled) {
            debugger ()->jump_to_position (a_location,
                                           &null_default_slot);
        } else {
            const IDebugger::Breakpoint *bp =
                get_breakpoint (a_location);
            THROW_IF_FAIL (bp);
            
            switch (a_location.kind ()) {
            case Loc::UNDEFINED_LOC_KIND:
                THROW ("Should not be reached");

            case Loc::SOURCE_LOC_KIND: {
                SourceLoc loc
                    (static_cast<const SourceLoc &> (a_location));
                JUMP_TO_LOC_AFTER_ENABLE_BP (loc);
            }
                break;
            case Loc::FUNCTION_LOC_KIND: {
                FunctionLoc loc
                    (static_cast<const FunctionLoc &> (a_location));
                JUMP_TO_LOC_AFTER_ENABLE_BP (loc);
            }
                break;
            case Loc::ADDRESS_LOC_KIND: {
                AddressLoc loc
                    (static_cast<const AddressLoc &> (a_location));
                JUMP_TO_LOC_AFTER_ENABLE_BP (loc);
            }
                break;
            }
        }
    } else {
        switch (a_location.kind ()) {
        case Loc::UNDEFINED_LOC_KIND:
            THROW ("Should not be reached");

        case Loc::SOURCE_LOC_KIND: {
            SourceLoc loc
                (static_cast<const SourceLoc &> (a_location));
            JUMP_TO_LOC_AFTER_SET_BP (loc);
        }
            break;
        case Loc::FUNCTION_LOC_KIND: {
            FunctionLoc loc
                (static_cast<const FunctionLoc &> (a_location));
            JUMP_TO_LOC_AFTER_SET_BP (loc);
        }
            break;
        case Loc::ADDRESS_LOC_KIND: {
            AddressLoc loc
                (static_cast<const AddressLoc &> (a_location));
            JUMP_TO_LOC_AFTER_SET_BP (loc);
        }
            break;
        }
    }
}

/// Set a breakpoint to the location selected by the user in the
/// source editor and jump (transfer execution of the inferior) there.
void
DBGPerspective::do_jump_and_break_to_current_location ()
{
    THROW_IF_FAIL (m_priv);
    SourceEditor *editor = get_current_source_editor ();
    THROW_IF_FAIL (editor);

    SafePtr<const Loc> loc (editor->current_location ());
    if (!loc) {
        LOG_DD ("Got an empty location.  Getting out.");
        return;
    }
    do_jump_and_break_to_location (*loc);
}

/// Jump (transfert execution of the inferior) to a given source
/// location.  This is a callback for the
/// IDebugger::breakpoint_set_signal signal.
///
/// \param a_loc the location to jump to.
void
DBGPerspective::jump_to_location (const map<string, IDebugger::Breakpoint>&,
                                  const Loc &a_loc)
{
    debugger ()->jump_to_position (a_loc, &null_default_slot);
}

/// Jump (transfert execution of the inferior) to a given location
/// that is specified by a dialog.  The dialog must have filled by the
/// user prior to calling this function.
///
/// \param a_dialog the dialog that contains the location specified by
/// the user.
void
DBGPerspective::jump_to_location_from_dialog (const SetJumpToDialog &a_dialog)
{
    SafePtr<const Loc> location (a_dialog.get_location ());
    if (!location
        || location->kind ()== Loc::UNDEFINED_LOC_KIND)
        return;
    if (a_dialog.get_break_at_location ())
        do_jump_and_break_to_location (*location);
    else
        debugger ()->jump_to_position (*location, &null_default_slot);
}

void
DBGPerspective::set_breakpoint ()
{
    SourceEditor *source_editor = get_current_source_editor ();
    THROW_IF_FAIL (source_editor);
    UString path;
    source_editor->get_path (path);
    THROW_IF_FAIL (path != "");

    //line numbers start at 0 in GtkSourceView, but at 1 in GDB <grin/>
    //so in DBGPerspective, the line number are set in the GDB's reference.

    gint current_line =
        source_editor->source_view ().get_source_buffer ()->get_insert
            ()->get_iter ().get_line () + 1;
    set_breakpoint (path, current_line,
                    /*condition=*/"",
                    /*is_countpoint*/false);
}

void
DBGPerspective::set_breakpoint (const UString &a_file_path,
                                int a_line,
                                const UString &a_condition,
                                bool a_is_count_point)
{
    LOG_DD ("set bkpoint request for " << a_file_path << ":" << a_line
            << " condition: '" << a_condition << "'");
        // only try to set the breakpoint if it's a reasonable value
        if (a_line && a_line != INT_MAX && a_line != INT_MIN) {
            debugger ()->set_breakpoint (a_file_path, a_line, a_condition,
                                         a_is_count_point ? -1 : 0);
        } else {
            LOG_ERROR ("invalid line number: " << a_line);
            UString msg;
            msg.printf (_("Invalid line number: %i"), a_line);
            display_warning (msg);
        }
}

void
DBGPerspective::set_breakpoint (const UString &a_func_name,
                                const UString &a_condition,
                                bool a_is_count_point)
{
    LOG_DD ("set bkpoint request in func" << a_func_name);
    debugger ()->set_breakpoint (a_func_name, a_condition,
                                 a_is_count_point ? -1 : 0);
}

void
DBGPerspective::set_breakpoint (const Address &a_address,
                                bool a_is_count_point)
{
    debugger ()->set_breakpoint (a_address, "",
                                 a_is_count_point ? -1 : 0);
}

void
DBGPerspective::set_breakpoint (const IDebugger::Breakpoint &a_breakpoint)
{
    UString file_name = a_breakpoint.file_full_name ().empty ()
        ? a_breakpoint.file_name ()
        : a_breakpoint.file_full_name ();

    // If the breakpoint was marked as 'disabled' in the session DB,
    // we have to set it and immediately disable it.  We need to pass
    // along some additional information in the 'cookie' to determine
    // which breakpoint needs to be disabling after it is set.
    UString cookie = a_breakpoint.enabled ()
        ? ""
        : "initially-disabled#" + file_name
        + "#" + UString::from_int (a_breakpoint.line ());

    // Now set the breakpoint proper.
    if (a_breakpoint.type () == IDebugger::Breakpoint::STANDARD_BREAKPOINT_TYPE
        || a_breakpoint.type () == IDebugger::Breakpoint::COUNTPOINT_TYPE) {
        int ignore_count =
            debugger ()->is_countpoint (a_breakpoint)
            ? -1
            : a_breakpoint.initial_ignore_count ();

        if (!file_name.empty ())
            debugger ()->set_breakpoint (file_name,
                                         a_breakpoint.line (),
                                         a_breakpoint.condition (),
                                         ignore_count, cookie);
        else if (!a_breakpoint.address ().empty ())
            debugger ()->set_breakpoint (a_breakpoint.address (),
                                         a_breakpoint.condition (),
                                         ignore_count, cookie);
        // else we don't set this breakpoint as it has neither an
        // address or a file name associated.
    } else if (a_breakpoint.type ()
               == IDebugger::Breakpoint::WATCHPOINT_TYPE) {
        debugger ()->set_watchpoint (a_breakpoint.expression (),
                                     a_breakpoint.is_write_watchpoint (),
                                     a_breakpoint.is_read_watchpoint ());
    }
}

/// Re-set ignore count on breakpoints that are already set.
void
DBGPerspective::re_initialize_set_breakpoints ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    typedef map<string, IDebugger::Breakpoint> BPMap;
    BPMap &bps = m_priv->breakpoints;

    // Re-set ignore count on set breakpoints.
    for (BPMap::const_iterator i = bps.begin ();
         i != bps.end ();
         ++i) {
        debugger ()->set_breakpoint_ignore_count
            (i->second.id (),
             i->second.initial_ignore_count ());
    }
}

/// Given a breakpoint that was set in the inferior, graphically
/// represent it and show it to the user.
///
/// Note that this function doesn't not try to set a breakpoint that
/// is reported to be 'pending' because then we are not even sure if
/// source code exists for that breakpoint.
///
/// \param a_breakpoint the breakpoint that was set.
void
DBGPerspective::append_breakpoint (const IDebugger::Breakpoint &a_breakpoint)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    UString file_path;
    file_path = a_breakpoint.file_full_name ();
    IDebugger::Breakpoint::Type type = a_breakpoint.type ();
    SourceEditor *editor = 0;

    if ((type == IDebugger::Breakpoint::STANDARD_BREAKPOINT_TYPE
         || type == IDebugger::Breakpoint::COUNTPOINT_TYPE)
        && file_path.empty ()) {
        file_path = a_breakpoint.file_name ();
    }

    m_priv->breakpoints[a_breakpoint.id ()] = a_breakpoint;
    m_priv->breakpoints[a_breakpoint.id ()].file_full_name (file_path);

    if (// We don't know how to graphically represent non-standard
        // breakpoints (e.g watchpoints) at this moment, so let's not
        // bother trying to graphically represent them.
        (type != IDebugger::Breakpoint::STANDARD_BREAKPOINT_TYPE
         && type != IDebugger::Breakpoint::COUNTPOINT_TYPE)
        // Let's not bother trying to to graphically represent a
        // pending breakpoint, either.
        || a_breakpoint.is_pending ())
        return;

    editor = get_or_append_source_editor_from_path (file_path);

    if (editor) {
        // We could find an editor for the file of the breakpoint.
        // Set the visual breakpoint at the breakpoint source line.
        SourceEditor::BufferType type = editor->get_buffer_type ();
        switch (type) {
            case SourceEditor::BUFFER_TYPE_SOURCE:
                append_visual_breakpoint (editor, a_breakpoint.line (),
                                          debugger ()->is_countpoint
                                          (a_breakpoint),
                                          a_breakpoint.enabled ());
                break;
            case SourceEditor::BUFFER_TYPE_ASSEMBLY:
                append_visual_breakpoint (editor, a_breakpoint.address (),
                                          debugger ()->is_countpoint
                                          (a_breakpoint),
                                          a_breakpoint.enabled ());
                break;
            case SourceEditor::BUFFER_TYPE_UNDEFINED:
                break;
        }
    } else if (!a_breakpoint.has_multiple_locations ()) {
        // We not could find an editor for the file of the breakpoint.
        // Ask the backend for asm instructions and set the visual breakpoint
        // at the breakpoint address.
        Glib::RefPtr<Gsv::Buffer> buf;
        editor = get_source_editor_from_path (get_asm_title ());
        if (editor == 0) {
            editor = create_source_editor (buf,
                                           /*asm_view=*/true,
                                           get_asm_title (),
                                           /*current_line=*/-1,
                                           /*current_address=*/"");
            append_source_editor (*editor, "");
        }
        THROW_IF_FAIL (editor);
        Address addr (a_breakpoint.address ());
        IDebugger::DisassSlot set_bp =
            sigc::bind (sigc::mem_fun (this,
                                       &DBGPerspective::on_debugger_asm_signal3),
                        editor,
                        a_breakpoint);
        disassemble_around_address_and_do (addr, set_bp);
    }

    if (a_breakpoint.has_multiple_locations ()) {
        vector<IDebugger::Breakpoint>::const_iterator i;
        for (i = a_breakpoint.sub_breakpoints ().begin ();
             i != a_breakpoint.sub_breakpoints ().end ();
             ++i)
            append_breakpoint (*i);
    }
}

void
DBGPerspective::append_breakpoints
                        (const map<string, IDebugger::Breakpoint> &a_breaks)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    map<string, IDebugger::Breakpoint>::const_iterator iter;
    for (iter = a_breaks.begin (); iter != a_breaks.end (); ++iter)
        append_breakpoint (iter->second);
}

/// Return the breakpoint that was set at a given location.
///
/// \param a_location the location to consider
///
/// \return the breakpoint found at the given location, 0 if none was
/// found.
const IDebugger::Breakpoint*
DBGPerspective::get_breakpoint (const Loc &a_location) const
{
    switch (a_location.kind ()) {
    case Loc::UNDEFINED_LOC_KIND:
        return 0;
    case Loc::SOURCE_LOC_KIND: {
        const SourceLoc &loc =
            static_cast<const SourceLoc&> (a_location);
        return get_breakpoint (loc.file_path (), loc.line_number ());
    }
    case Loc::FUNCTION_LOC_KIND: {
        // TODO: For now we cannot get a breakpoint set by function.
        // For that we would need to be able to get the address at
        // which a breakpoint set by function would be set to.
        return 0;
    }
    case Loc::ADDRESS_LOC_KIND: {
        const AddressLoc &loc =
            static_cast<const AddressLoc&> (a_location);
        return get_breakpoint (loc.address ());
    }
    }
    // Should not be reached.
    return 0;
}

const IDebugger::Breakpoint*
DBGPerspective::get_breakpoint (const UString &a_file_name,
                                int a_line_num) const
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    UString breakpoint = a_file_name + ":" + UString::from_int (a_line_num);

    map<string, IDebugger::Breakpoint>::const_iterator iter;
    for (iter = m_priv->breakpoints.begin ();
         iter != m_priv->breakpoints.end ();
         ++iter) {
        LOG_DD ("got breakpoint " << iter->second.file_full_name ()
                << ":" << iter->second.line () << "...");
        // because some versions of gdb don't
        // return the full file path info for
        // breakpoints, we have to also check to see
        // if the basenames match
        if (((iter->second.file_full_name () == a_file_name)
             || (Glib::path_get_basename (iter->second.file_full_name ())
                     == Glib::path_get_basename (a_file_name)))
            && (iter->second.line () == a_line_num)) {
            LOG_DD ("found breakpoint !");
            return &iter->second;
        }
    }
    LOG_DD ("did not find breakpoint");
    return 0;
}

const IDebugger::Breakpoint*
DBGPerspective::get_breakpoint (const Address &a) const
{
    map<string, IDebugger::Breakpoint>::const_iterator iter;
    for (iter = m_priv->breakpoints.begin ();
         iter != m_priv->breakpoints.end ();
         ++iter) {
        if (a == iter->second.address ()) {
            return &iter->second;
        }
    }
    return 0;
}

bool
DBGPerspective::delete_breakpoint ()
{
    SourceEditor *source_editor = get_current_source_editor ();
    THROW_IF_FAIL (source_editor);
    UString path;
    source_editor->get_path (path);
    THROW_IF_FAIL (path  != "");

    gint current_line =
        source_editor->source_view ().get_source_buffer ()->get_insert
            ()->get_iter ().get_line () + 1;

    const IDebugger::Breakpoint *bp;
    if ((bp = get_breakpoint (path, current_line)) == 0) {
        return false;
    }
    return delete_breakpoint (bp->id ());
}

bool
DBGPerspective::delete_breakpoint (const string &a_breakpoint_num)
{
    map<string, IDebugger::Breakpoint>::iterator iter =
        m_priv->breakpoints.find (a_breakpoint_num);
    if (iter == m_priv->breakpoints.end ()) {
        LOG_ERROR ("breakpoint " << a_breakpoint_num << " not found");
        return false;
    }
    debugger ()->delete_breakpoint (a_breakpoint_num);
    return true;
}

// Popup a dialog asking the user to select the file a_file_name.
// \param a_file_name the name of the file we want the user to select.
// \param a_selected_file_path the path to the file the user actually
// selected.
// \return true if the user really selected the file we wanted, false
// otherwise.
bool
DBGPerspective::ask_user_to_select_file (const UString &a_file_name,
                                         UString &a_selected_file_path)
{
    return ui_utils::ask_user_to_select_file (a_file_name, m_priv->prog_cwd,
                                              a_selected_file_path);
}

bool
DBGPerspective::append_visual_breakpoint (SourceEditor *a_editor,
                                          int a_linenum,
                                          bool a_is_countpoint,
                                          bool a_enabled)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD

    if (a_editor == 0)
        return false;
    return a_editor->set_visual_breakpoint_at_line (a_linenum,
                                                    a_is_countpoint,
                                                    a_enabled);
}

bool
DBGPerspective::append_visual_breakpoint (SourceEditor *a_editor,
                                          const Address &a_address,
                                          bool a_is_countpoint,
                                          bool a_enabled)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD

    if (a_editor == 0)
        return false;
    return a_editor->set_visual_breakpoint_at_address (a_address,
                                                       a_is_countpoint,
                                                       a_enabled);
}

void
DBGPerspective::delete_visual_breakpoint (const UString &a_file_name,
                                          int a_linenum)
{
    SourceEditor *source_editor = get_source_editor_from_path (a_file_name);
    if (!source_editor) {
        source_editor = get_source_editor_from_path (a_file_name, true);
    }
    THROW_IF_FAIL (source_editor);
    source_editor->remove_visual_breakpoint_from_line (a_linenum);
}

void
DBGPerspective::delete_visual_breakpoint (const string &a_breakpoint_num)
{
    map<string, IDebugger::Breakpoint>::iterator iter =
        m_priv->breakpoints.find (a_breakpoint_num);
    if (iter == m_priv->breakpoints.end ())
        return;
    delete_visual_breakpoint (iter);
}

void
DBGPerspective::delete_visual_breakpoint (map<string, IDebugger::Breakpoint>::iterator &a_i)
{
    SourceEditor *source_editor = 0;

    UString file_name = a_i->second.file_name ();
    if (file_name.empty ())
        file_name = a_i->second.file_full_name ();
    if (!file_name.empty ()) {
        get_source_editor_from_path (file_name);
        if (!source_editor)
            source_editor =
                get_source_editor_from_path (file_name,
                                             true);
    } else {
        source_editor = get_source_editor_from_path (get_asm_title ());
    }

    if (source_editor == 0)
        // This can happen for a BP with no debug info, but for which
        // we haven't done any disassembling yet.
        return;

    switch (source_editor->get_buffer_type ()) {
    case SourceEditor::BUFFER_TYPE_ASSEMBLY:
        source_editor->remove_visual_breakpoint_from_address
            (a_i->second.address ());
        break;
    case SourceEditor::BUFFER_TYPE_SOURCE:
        source_editor->remove_visual_breakpoint_from_line
            (a_i->second.line ());
        break;
    case SourceEditor::BUFFER_TYPE_UNDEFINED:
        THROW ("should not be reached");
        break;
    }

    LOG_DD ("going to erase breakpoint number " << a_i->first);
    m_priv->breakpoints.erase (a_i);

}

void
DBGPerspective::delete_visual_breakpoints ()
{
    if (m_priv->breakpoints.empty ())
        return;

    map<string, IDebugger::Breakpoint> bps = m_priv->breakpoints;
    map<string, IDebugger::Breakpoint>::iterator iter;

    for (iter = bps.begin (); iter != bps.end (); ++iter)
        delete_visual_breakpoint (iter->first);
}

void
DBGPerspective::choose_function_overload
                (const vector<IDebugger::OverloadsChoiceEntry> &a_entries)
{
    if (a_entries.empty ()) {
        LOG_DD ("got an empty list of overloads choice");
        return;
    }
    THROW_IF_FAIL (debugger ());
    ChooseOverloadsDialog dialog (plugin_path (), a_entries);
    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        debugger ()->choose_function_overload (0)/*cancel*/;
        return;
    }
    vector<IDebugger::OverloadsChoiceEntry> overloads =
                                            dialog.overloaded_functions ();

    vector<IDebugger::OverloadsChoiceEntry>::const_iterator it;
    vector<int> nums;
    for (it = overloads.begin (); it != overloads.end (); ++it) {
        nums.push_back (it->index ());
    }
    if (!nums.empty ())
        debugger ()->choose_function_overloads (nums);
}

void
DBGPerspective::remove_visual_decorations_from_text
                                            (const UString &a_file_path)
{
    SourceEditor *editor = get_source_editor_from_path (a_file_path);
    if (editor == 0)
        return;
    editor->clear_decorations ();
}

bool
DBGPerspective::apply_decorations (const UString &a_file_path)
{
    SourceEditor *editor = get_source_editor_from_path (a_file_path);
    RETURN_VAL_IF_FAIL (editor, false);

    return apply_decorations (editor);
}

bool
DBGPerspective::apply_decorations (SourceEditor *a_editor,
                                   bool a_scroll_to_where_marker)
{
    bool result = false;
    if (a_editor == 0)
        return result;

    SourceEditor::BufferType type = a_editor->get_buffer_type ();
    switch (type) {
        case SourceEditor::BUFFER_TYPE_SOURCE:
            result = apply_decorations_to_source (a_editor,
                                                  a_scroll_to_where_marker);
            break;
        case SourceEditor::BUFFER_TYPE_ASSEMBLY:
            result = apply_decorations_to_asm (a_editor,
                                               a_scroll_to_where_marker);
            break;
        case SourceEditor::BUFFER_TYPE_UNDEFINED:
            break;
    }
    return result;
}

bool
DBGPerspective::apply_decorations_to_source (SourceEditor *a_editor,
                                             bool a_scroll_to_where_marker)
{
    if (a_editor == 0)
        return false;

    THROW_IF_FAIL (a_editor->get_buffer_type ()
                   == SourceEditor::BUFFER_TYPE_SOURCE);

    map<string, IDebugger::Breakpoint>::const_iterator it;
    for (it = m_priv->breakpoints.begin ();
         it != m_priv->breakpoints.end ();
         ++it) {
        if (a_editor->get_path () == it->second.file_full_name ()) {
            append_visual_breakpoint (a_editor,
                                      it->second.line (),
                                      debugger ()->is_countpoint
                                      (it->second),
                                      it->second.enabled ());
        }
    }

    // If we don't want to scroll to the "where marker",
    // the scroll to the line that was precedently selected
    int cur_line;
    if (!a_scroll_to_where_marker
        && (cur_line = a_editor->current_line ()) > 0) {
        LOG_DD ("scroll to cur_line: " << (int)cur_line);
        Gtk::TextBuffer::iterator iter =
        a_editor->source_view ().get_buffer ()->get_iter_at_line (cur_line);
        if (iter)
            a_editor->source_view ().get_buffer ()->place_cursor (iter);
        a_editor->scroll_to_line (cur_line);
    }

    if (get_current_source_editor (false) == a_editor)
        set_where (a_editor,
                   m_priv->current_frame.line (),
                   /*a_scroll_to_where_marker=*/true);
    return true;
}

bool
DBGPerspective::apply_decorations_to_asm (SourceEditor *a_editor,
                                          bool a_scroll_to_where_marker,
                                          bool a_approximate_where)
{
    if (a_editor == 0)
        return false;

    THROW_IF_FAIL (a_editor->get_buffer_type ()
                   == SourceEditor::BUFFER_TYPE_ASSEMBLY);

    /// Apply breakpoint decorations to the breakpoints that are
    /// within the address range currently displayed.
    map<string, IDebugger::Breakpoint>::const_iterator it;
    for (it = m_priv->breakpoints.begin ();
         it != m_priv->breakpoints.end ();
         ++it) {
        if (a_editor->get_path () == it->second.file_full_name ()) {
            Address addr = it->second.address ();
            if (!append_visual_breakpoint (a_editor, addr,
                                           debugger ()->is_countpoint
                                           (it->second),
                                           it->second.enabled ())) {
                LOG_DD ("Could'nt find line for address: "
                        << addr.to_string ()
                        << " for file: "
                        << a_editor->get_path ());
            }
        }
    }

    // If we don't want to scroll to the "where marker", then scroll to
    // the line that was precedently selected
    int cur_line;
    if (!a_scroll_to_where_marker
        && (cur_line = a_editor->current_line ()) > 0) {
        LOG_DD ("scroll to cur_line: " << cur_line);
        Gtk::TextBuffer::iterator iter =
        a_editor->source_view ().get_buffer ()->get_iter_at_line (cur_line);
        if (iter)
            a_editor->source_view ().get_buffer ()->place_cursor (iter);
        a_editor->scroll_to_line (cur_line);
    }

    // Now apply decoration to the where marker. If the address of the
    // where marker is not within the address range currently
    // displayed, then disassemble instructions around the where
    // marker's address, display that, and move the where marker
    // to the correct address.
    if (get_current_source_editor () == a_editor)
        set_where (a_editor,
                   m_priv->current_frame.address (),
                   a_scroll_to_where_marker,
                   /*try_hard=*/true,
                   a_approximate_where);
    return true;
}

bool
DBGPerspective::delete_breakpoint (const UString &a_file_name,
                                   int a_line_num)
{
    bool found = false;
    map<string, IDebugger::Breakpoint>::const_iterator iter;
    for (iter = m_priv->breakpoints.begin ();
         iter != m_priv->breakpoints.end ();
         ++iter) {
        if (((iter->second.file_full_name () == a_file_name)
             || (Glib::path_get_basename (iter->second.file_full_name ())
                 == Glib::path_get_basename (a_file_name)))
            && (iter->second.line () == a_line_num)) {
            delete_breakpoint (iter->first);
            found = true;
        }
    }
    return found;
}

bool
DBGPerspective::delete_breakpoint (const Address &a_address)
{
    const IDebugger::Breakpoint *bp;
    if ((bp = get_breakpoint (a_address)) == 0)
        return false;
    return delete_breakpoint (bp->id ());
}

/// Return true if a breakpoint was set at a given location.
///
/// \param a_location the location to consider
///
/// \return true if there was a breakpoint set at the given location
bool
DBGPerspective::is_breakpoint_set_at_location (const Loc &a_location,
                                               bool &a_enabled)
{
    switch (a_location.kind ()) {
    case Loc::UNDEFINED_LOC_KIND:
        return false;
    case Loc::SOURCE_LOC_KIND: {
        const SourceLoc &loc =
            static_cast<const SourceLoc&> (a_location);
        return is_breakpoint_set_at_line (loc.file_path (),
                                          loc.line_number (),
                                          a_enabled);
    }
        break;
    case Loc::FUNCTION_LOC_KIND:
        // Grrr, for now we cannot know if a breakpoint was set at a
        // function's entry point.
        return false;
        break;
    case Loc::ADDRESS_LOC_KIND: {
        const AddressLoc &loc =
            static_cast<const AddressLoc&> (a_location);
        return is_breakpoint_set_at_address (loc.address (),
                                             a_enabled);
    }
        break;
    }
    // Should not be reached
    return false;
}

/// Test whether if a breakpoint was set at a source location.
///
/// \param a_file_path the path of the file to consider
///
/// \param a_line_num the line number of the location to consider
///
/// \param a_enabled iff the function returns true, this boolean is
/// set.  It's value is then either true if the breakpoint found at
/// the given source location is enabled, false otherwise.
bool
DBGPerspective::is_breakpoint_set_at_line (const UString &a_file_path,
                                           int a_line_num,
                                           bool &a_enabled)
{
    const IDebugger::Breakpoint *bp;
    if ((bp = get_breakpoint (a_file_path, a_line_num)) != 0) {
        a_enabled = bp->enabled ();
        return true;
    }
    return false;
}

/// Test whether if a breakpoint was set at a given address.
///
/// \param a_address the address to consider.
///
/// \param a_enabled iff the function returns true, this boolean is
/// set.  It's value is then either true if the breakpoint found at
/// the given address is enabled, false otherwise.
///
/// \return true if a breakpoint is set at the given address, false
/// otherwise.
bool
DBGPerspective::is_breakpoint_set_at_address (const Address &a_address,
                                              bool &a_enabled)
{
    const IDebugger::Breakpoint *bp;
    if ((bp = get_breakpoint (a_address)) != 0) {
        a_enabled = bp->enabled ();
        return true;
    }
    return false;
}

void
DBGPerspective::toggle_breakpoint (const UString &a_file_path,
                                   int a_line_num)
{
    LOG_DD ("file_path:" << a_file_path
           << ", line_num: " << a_file_path);

    bool enabled=false;
    if (is_breakpoint_set_at_line (a_file_path, a_line_num, enabled)) {
        LOG_DD ("breakpoint set already, delete it!");
        delete_breakpoint (a_file_path, a_line_num);
    } else {
        LOG_DD ("breakpoint no set yet, set it!");
        set_breakpoint (a_file_path, a_line_num,
                        /*condition=*/"",
                        /*is_count_point=*/false);
    }
}

void
DBGPerspective::toggle_breakpoint (const Address &a_address)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    bool enabled = false;
    if (is_breakpoint_set_at_address (a_address, enabled)) {
        delete_breakpoint (a_address);
    } else {
        set_breakpoint (a_address, /*is_count_point=*/false);
    }
}

void
DBGPerspective::toggle_breakpoint ()
{
    SourceEditor *source_editor = get_current_source_editor ();
    THROW_IF_FAIL (source_editor);
    UString path;
    source_editor->get_path (path);
    THROW_IF_FAIL (path != "");

    switch (source_editor->get_buffer_type ()) {
    case SourceEditor::BUFFER_TYPE_ASSEMBLY:{
        Address a;
        if (source_editor->current_address (a))
            toggle_breakpoint (a);
    }
        break;
    case SourceEditor::BUFFER_TYPE_SOURCE: {
        int current_line = source_editor->current_line ();
        if (current_line >= 0)
            toggle_breakpoint (path, current_line);
    }
        break;
    default:
        THROW ("should not be reached");
        break;
    }
}

void
DBGPerspective::toggle_countpoint (const UString &a_file_path,
                                   int a_linenum)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    LOG_DD ("file_path:" << a_file_path
            << ", line_num: " << a_file_path);

    const IDebugger::Breakpoint *bp;
    if ((bp = get_breakpoint (a_file_path, a_linenum)) != 0) {
        // So a breakpoint is already set. See if it's a
        // countpoint. If yes, turn it into a normal
        // breakpoint. Otherwise, turn it into a count point.
        bool enable_cp = !debugger ()->is_countpoint (*bp);
        debugger ()->enable_countpoint (bp->id (), enable_cp);
    } else {
        // No breakpoint is set on this line. Set a new countpoint.
        set_breakpoint (a_file_path, a_linenum,
                        /*condition=*/"",
                        /*is_count_point=*/true);
    }
}

void
DBGPerspective::toggle_countpoint (const Address &a_address)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    const IDebugger::Breakpoint *bp;
    if ((bp = get_breakpoint (a_address)) != 0) {
        // So a breakpoint is already set. See if it's a
        // countpoint. If yes, turn it into a normal
        // breakpoint. Otherwise, turn it into a count point.

        bool enable_cp = true;
        if (debugger ()->is_countpoint (*bp))
            enable_cp = false;

        debugger ()->enable_countpoint (bp->id (), enable_cp);
    } else {
        // No breakpoint is set on this line. Set a new countpoint.
        set_breakpoint (a_address, /*is_count_point=*/true);
    }
}

void
DBGPerspective::toggle_countpoint (void)
{
    SourceEditor *source_editor = get_current_source_editor ();

    switch (source_editor->get_buffer_type ()) {
    case SourceEditor::BUFFER_TYPE_SOURCE: {
        int line =  source_editor->current_line ();
        UString path;
        source_editor->get_path (path);
        toggle_countpoint (path, line);
    }
        break;
    case SourceEditor::BUFFER_TYPE_ASSEMBLY: {
        Address a;
        source_editor->current_address (a);
        toggle_countpoint (a);
    }
        break;
    default:
        THROW ("should not be reached");
    }
}

void
DBGPerspective::set_breakpoint_from_dialog (SetBreakpointDialog &a_dialog)
{
    bool is_count_point = a_dialog.count_point ();

    switch (a_dialog.mode ()) {
        case SetBreakpointDialog::MODE_SOURCE_LOCATION:
            {
                UString filename;
                filename = a_dialog.file_name ();
                if (filename.empty ()) {
                    // if the user didn't set any filename, let's assume
                    // she wants to set a breakpoint in the current file.
                    SourceEditor *source_editor =
                                        get_current_source_editor ();
                    THROW_IF_FAIL (source_editor);
                    source_editor->get_file_name (filename);
                    THROW_IF_FAIL (!filename.empty ());
                    LOG_DD ("setting filename to current file name: "
                            << filename);
                }
                int line = a_dialog.line_number ();
                LOG_DD ("setting breakpoint in file "
                        << filename << " at line " << line);
                set_breakpoint (filename, line,
                                a_dialog.condition (),
                                is_count_point);
                break;
            }

        case SetBreakpointDialog::MODE_FUNCTION_NAME:
            {
                UString function = a_dialog.function ();
                THROW_IF_FAIL (function != "");
                LOG_DD ("setting breakpoint at function: " << function);
                set_breakpoint (function, a_dialog.condition (),
                                is_count_point);
                break;
            }

        case SetBreakpointDialog::MODE_BINARY_ADDRESS:
            {
                Address address = a_dialog.address ();
                if (!address.empty ()) {
                    LOG_DD ("setting breakpoint at address: "
                            << address);
                    set_breakpoint (address, is_count_point);
                }
                break;
            }

        case SetBreakpointDialog::MODE_EVENT:
            {
                UString event = a_dialog.event ();
                THROW_IF_FAIL (event != "");
                debugger ()->set_catch (event);
                break;
            }

        default:
            THROW ("should not be reached");
            break;
    }
}

void
DBGPerspective::set_breakpoint_at_current_line_using_dialog ()
{
    SourceEditor *source_editor = get_current_source_editor ();
    THROW_IF_FAIL (source_editor);
    UString path;
    source_editor->get_path (path);
    THROW_IF_FAIL (path != "");
    int current_line =
        source_editor->source_view ().get_source_buffer ()->get_insert
                ()->get_iter ().get_line () + 1;
    if (current_line >= 0)
        set_breakpoint_using_dialog (path, current_line);
}

void
DBGPerspective::set_breakpoint_using_dialog ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    SetBreakpointDialog dialog (plugin_path ());

    // Checkout if the user did select a function number.
    // If she did, pre-fill the breakpoint setting dialog with the
    // function name so that if the user hits enter, a breakpoint is set
    // to that function by default.

    UString function_name;
    SourceEditor *source_editor = get_current_source_editor ();

    if (source_editor) {
        Glib::RefPtr<Gsv::Buffer> buffer =
            source_editor->source_view ().get_source_buffer ();
        THROW_IF_FAIL (buffer);

        Gtk::TextIter start, end;
        if (buffer->get_selection_bounds (start, end)) {
            function_name = buffer->get_slice (start, end);
        }
    }
    if (!function_name.empty ()) {
        // really the default function name to break into, by default.
        dialog.mode (SetBreakpointDialog::MODE_FUNCTION_NAME);
        dialog.function (function_name);
    }

    // Pheew. Enough set up for now. Time to launch the dialog and get the
    // ball rolling.
    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        return;
    }

    // Get what the user set in the dialog and really ask the backend
    // to set the breakpoint accordingly.
    set_breakpoint_from_dialog (dialog);
}

void
DBGPerspective::set_breakpoint_using_dialog (const UString &a_file_name,
                                             const int a_line_num)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    THROW_IF_FAIL (!a_file_name.empty ());
    THROW_IF_FAIL (a_line_num > 0);

    SetBreakpointDialog dialog (plugin_path ());
    dialog.mode (SetBreakpointDialog::MODE_SOURCE_LOCATION);
    dialog.file_name (a_file_name);
    dialog.line_number (a_line_num);
    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        return;
    }
    set_breakpoint_from_dialog (dialog);
}

void
DBGPerspective::set_breakpoint_using_dialog (const UString &a_function_name)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    SetBreakpointDialog dialog (plugin_path ());
    dialog.mode (SetBreakpointDialog::MODE_FUNCTION_NAME);
    dialog.file_name (a_function_name);
    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        return;
    }
    set_breakpoint_from_dialog (dialog);
}

void
DBGPerspective::set_watchpoint_using_dialog ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    WatchpointDialog dialog (plugin_path (), *debugger (), *this);
    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK) {
        return;
    }

    UString expression = dialog.expression ();
    if (expression.empty ())
        return;

    WatchpointDialog::Mode mode = dialog.mode ();
    debugger ()->set_watchpoint (expression,
                                 mode & WatchpointDialog::WRITE_MODE,
                                 mode & WatchpointDialog::READ_MODE);
}

void
DBGPerspective::refresh_locals ()
{

    THROW_IF_FAIL (m_priv);
    get_local_vars_inspector ().show_local_variables_of_current_function
                                                        (m_priv->current_frame);
}

void
DBGPerspective::disassemble (bool a_show_asm_in_new_tab)
{
    THROW_IF_FAIL (m_priv);

    IDebugger::DisassSlot slot;

    if (a_show_asm_in_new_tab)
        slot =
            sigc::bind (sigc::mem_fun (this,
                                       &DBGPerspective::on_debugger_asm_signal1),
                        true);
    else
        slot =
            sigc::bind (sigc::mem_fun (this,
                                       &DBGPerspective::on_debugger_asm_signal1),
                        false);

    disassemble_and_do (slot);
}

void
DBGPerspective::disassemble_and_do (IDebugger::DisassSlot &a_what_to_do,
                                    bool a_tight)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    // If we don't have the current instruction pointer (IP), there is
    // nothing we can do.
    if (!debugger ()->is_attached_to_target ()
        || m_priv->current_frame.has_empty_address ()) {
        LOG_DD ("No current instruction pointer");
        return;
    }

    Range addr_range (m_priv->current_frame.address (),
                      m_priv->current_frame.address ());
    get_frame_breakpoints_address_range (m_priv->current_frame, addr_range);

    // Increase the address range of instruction to disassemble by a
    // number N that is equal to m_priv->num_instr_to_disassemble.
    // 17 is the max size (in bytes) of an instruction on intel
    // archictecture. So let's say N instructions on IA is at
    // maximum N x 17.
    // FIXME: find a way to make this more cross arch.
    size_t max = (a_tight)
        ? addr_range.max () + 17
        : addr_range.max () + m_priv->num_instr_to_disassemble * 17;

    addr_range.max (max);

    THROW_IF_FAIL (addr_range.min () != addr_range.max ());

    debugger ()->disassemble (/*start_addr=*/addr_range.min (),
                              /*start_addr_relative_to_pc=*/false,
                              /*end_addr=*/addr_range.max (),
                              /*end_addr_relative_to_pc=*/false,
                              a_what_to_do,
                              m_priv->asm_style_pure);
}

void
DBGPerspective::disassemble_around_address_and_do
                                (const Address &a_address,
                                 IDebugger::DisassSlot &a_what_to_do)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    if (!debugger ()->is_attached_to_target ()
        || a_address.empty ()) {
        LOG_DD ("No current instruction pointer");
        return;
    }

    if (a_address.empty ())
        return;

    Range addr_range (a_address, a_address);
    // Increase the address range of instruction to disassemble by a
    // number N that is equal to m_priv->num_instr_to_disassemble.
    // 17 is the max size (in bytes) of an instruction on intel
    // archictecture. So let's say N instructions on IA is at
    // maximum N x 17.
    // FIXME: find a way to make this more cross arch.
    const size_t instr_size = 17;
    const size_t total_instrs_size =
        m_priv->num_instr_to_disassemble * instr_size;

    addr_range.max (addr_range.max () + total_instrs_size);
    THROW_IF_FAIL (addr_range.min () != addr_range.max ());

    debugger ()->disassemble (/*start_addr=*/addr_range.min (),
                              /*start_addr_relative_to_pc=*/false,
                              /*end_addr=*/addr_range.max (),
                              /*end_addr_relative_to_pc=*/false,
                              a_what_to_do,
                              m_priv->asm_style_pure);
}


void
DBGPerspective::toggle_breakpoint_enabled ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    SourceEditor *source_editor = get_current_source_editor ();
    THROW_IF_FAIL (source_editor);
    UString path;
    source_editor->get_path (path);
    THROW_IF_FAIL (path != "");

    switch (source_editor->get_buffer_type ()) {
    case SourceEditor::BUFFER_TYPE_ASSEMBLY: {
        Address a;
        if (source_editor->current_address (a)) {
            toggle_breakpoint_enabled (a);
        } else {
            LOG_DD ("Couldn't find any address here");
        }
    }
        break;
    case SourceEditor::BUFFER_TYPE_SOURCE: {
        int current_line = source_editor->current_line ();
        if (current_line >= 0)
            toggle_breakpoint_enabled (path, current_line);
    }
        break;
    default:
        THROW ("should not be reached");
        break;
    }
}

void
DBGPerspective::toggle_breakpoint_enabled (const UString &a_file_path,
                                           int a_line_num)
{
    LOG_DD ("file_path:" << a_file_path
            << ", line_num: " << a_line_num);

    const IDebugger::Breakpoint *bp;
    if ((bp = get_breakpoint (a_file_path, a_line_num)) != 0)
        toggle_breakpoint_enabled (bp->id (), bp->enabled ());
    else {
        LOG_DD ("breakpoint not set");
    }
}

void
DBGPerspective::toggle_breakpoint_enabled (const Address &a)
{
    LOG_DD ("address: " << a.to_string ());

    const IDebugger::Breakpoint *bp;
    if ((bp = get_breakpoint (a)) != 0)
        toggle_breakpoint_enabled (bp->id (), bp->enabled ());
    else
        LOG_DD ("breakpoint not set");
}

void
DBGPerspective::toggle_breakpoint_enabled (const string &a_break_num,
                                           bool a_enabled)
{
    LOG_DD ("enabled: " << a_enabled);

    if (a_enabled)
        debugger ()->disable_breakpoint (a_break_num);
    else
        debugger ()->enable_breakpoint (a_break_num);
}

void
DBGPerspective::update_src_dependant_bp_actions_sensitiveness ()
{
    Glib::RefPtr<Gtk::Action> toggle_break_action =
         workbench ().get_ui_manager ()->get_action
        ("/MenuBar/MenuBarAdditions/DebugMenu/ToggleBreakMenuItem");
    THROW_IF_FAIL (toggle_break_action);

    Glib::RefPtr<Gtk::Action> toggle_enable_action =
         workbench ().get_ui_manager ()->get_action
        ("/MenuBar/MenuBarAdditions/DebugMenu/ToggleEnableBreakMenuItem");
    THROW_IF_FAIL (toggle_enable_action);

    Glib::RefPtr<Gtk::Action> bp_at_cur_line_with_dialog_action =
         workbench ().get_ui_manager ()->get_action
        ("/MenuBar/MenuBarAdditions/DebugMenu/SetBreakUsingDialogMenuItem");
    THROW_IF_FAIL (bp_at_cur_line_with_dialog_action);

    Glib::RefPtr<Gtk::Action> toggle_countpoint_action =
         workbench ().get_ui_manager ()->get_action
        ("/MenuBar/MenuBarAdditions/DebugMenu/ToggleCountpointMenuItem");

    if (get_num_notebook_pages () == 0) {
        toggle_break_action->set_sensitive (false);
        toggle_enable_action->set_sensitive (false);
        bp_at_cur_line_with_dialog_action->set_sensitive (false);
        toggle_countpoint_action->set_sensitive (false);
    } else {
        toggle_break_action->set_sensitive (true);
        toggle_enable_action->set_sensitive (true);
        bp_at_cur_line_with_dialog_action->set_sensitive (true);
        toggle_countpoint_action->set_sensitive (true);
    }

}

void
DBGPerspective::inspect_expression ()
{
    THROW_IF_FAIL (m_priv);

    UString expression;
    Gtk::TextIter start, end;
    SourceEditor *source_editor = get_current_source_editor ();
    if (source_editor) {
        Glib::RefPtr<Gsv::Buffer> buffer =
            source_editor->source_view ().get_source_buffer ();
        THROW_IF_FAIL (buffer);
        if (buffer->get_selection_bounds (start, end)) {
            expression = buffer->get_slice (start, end);
        }
    }
    inspect_expression (expression);
}

void
DBGPerspective::inspect_expression (const UString &a_expression)
{
    THROW_IF_FAIL (debugger ());
    ExprInspectorDialog dialog (*debugger (),
                               *this);
    dialog.set_history (m_priv->var_inspector_dialog_history);
    dialog.expr_monitoring_requested ().connect
        (sigc::mem_fun (*this,
                        &DBGPerspective::on_expr_monitoring_requested));
    if (a_expression != "") {
        dialog.inspect_expression (a_expression);
    }
    dialog.run ();
    m_priv->var_inspector_dialog_history.clear ();
    dialog.get_history (m_priv->var_inspector_dialog_history);
}

void
DBGPerspective::call_function ()
{
    THROW_IF_FAIL (m_priv);

    CallFunctionDialog dialog (plugin_path ());

    // Fill the dialog with the "function call" expression history.
    if (!m_priv->call_expr_history.empty ())
        dialog.set_history (m_priv->call_expr_history);

    int result = dialog.run ();
    if (result != Gtk::RESPONSE_OK)
        return;

    UString call_expr = dialog.call_expression ();

    if (call_expr.empty ())
        return;

    // Update our copy of call expression history.
    dialog.get_history (m_priv->call_expr_history);

    // Really execute the function call expression now.
    call_function (call_expr);
}

void
DBGPerspective::call_function (const UString &a_call_expr)
{
    THROW_IF_FAIL (debugger ());

    if (!a_call_expr.empty ()) {
        // Print a little message on the terminal
        // saying that we are calling a_call_expr
        std::stringstream s;
        s << "<Nemiver call_function>"
            << a_call_expr.raw ()
            << "</Nemiver>"
            << "\n\r";
        get_terminal ().feed (s.str ());

        // Really hit the debugger now.
        debugger ()->call_function (a_call_expr);
    }
}


IDebuggerSafePtr&
DBGPerspective::debugger ()
{
    if (!m_priv->debugger) {
        DynamicModule::Loader *loader =
            workbench ().get_dynamic_module ().get_module_loader ();
        THROW_IF_FAIL (loader);

        DynamicModuleManager *module_manager =
                            loader->get_dynamic_module_manager ();
        THROW_IF_FAIL (module_manager);

        UString debugger_dynmod_name;

        NEMIVER_TRY
        get_conf_mgr ().get_key_value (CONF_KEY_DEBUGGER_ENGINE_DYNMOD_NAME,
                                       debugger_dynmod_name);
        NEMIVER_CATCH_NOX

        LOG_DD ("got debugger_dynmod_name from confmgr: '"
                << debugger_dynmod_name << "'");
        if (debugger_dynmod_name == "") {
            debugger_dynmod_name = "gdbengine";
        }
        LOG_DD ("using debugger_dynmod_name: '"
                << debugger_dynmod_name << "'");
        m_priv->debugger =
            module_manager->load_iface<IDebugger> (debugger_dynmod_name,
                                                   "IDebugger");
        IConfMgrSafePtr conf_mgr = workbench ().get_configuration_manager ();
        m_priv->debugger->do_init (conf_mgr);
        m_priv->debugger->set_event_loop_context
                                    (Glib::MainContext::get_default ());
    }
    THROW_IF_FAIL (m_priv->debugger);
    return m_priv->debugger;
}

IConfMgr&
DBGPerspective::get_conf_mgr ()
{
    IConfMgrSafePtr conf_mgr = workbench ().get_configuration_manager ();
    THROW_IF_FAIL (conf_mgr);
    return *conf_mgr;
}

CallStack&
DBGPerspective::get_call_stack ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->call_stack) {
        m_priv->call_stack.reset (new CallStack (debugger (),
                                                 workbench (), *this));
        THROW_IF_FAIL (m_priv);
    }
    return *m_priv->call_stack;
}

Gtk::HPaned&
DBGPerspective::get_context_paned ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->context_paned) {
        m_priv->context_paned.reset (new Gtk::HPaned ());
        THROW_IF_FAIL (m_priv->context_paned);
    }
    return *m_priv->context_paned;

}

Gtk::HPaned&
DBGPerspective::get_call_stack_paned ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->call_stack_paned) {
        m_priv->call_stack_paned.reset (new Gtk::HPaned ());
        THROW_IF_FAIL (m_priv->call_stack_paned);
    }
    return *m_priv->call_stack_paned;
}

Gtk::ScrolledWindow&
DBGPerspective::get_call_stack_scrolled_win ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->call_stack_scrolled_win) {
        m_priv->call_stack_scrolled_win.reset (new Gtk::ScrolledWindow ());
        m_priv->call_stack_scrolled_win->set_policy (Gtk::POLICY_AUTOMATIC,
                                                     Gtk::POLICY_AUTOMATIC);
        THROW_IF_FAIL (m_priv->call_stack_scrolled_win);
    }
    return *m_priv->call_stack_scrolled_win;
}

Gtk::ScrolledWindow&
DBGPerspective::get_thread_list_scrolled_win ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->thread_list_scrolled_win) {
        m_priv->thread_list_scrolled_win.reset (new Gtk::ScrolledWindow ());
        m_priv->thread_list_scrolled_win->set_policy (Gtk::POLICY_AUTOMATIC,
                                                      Gtk::POLICY_AUTOMATIC);
        THROW_IF_FAIL (m_priv->thread_list_scrolled_win);
    }
    return *m_priv->thread_list_scrolled_win;
}

LocalVarsInspector&
DBGPerspective::get_local_vars_inspector ()
{
    THROW_IF_FAIL (m_priv);
    THROW_IF_FAIL (m_priv->workbench);

    if (!m_priv->variables_editor) {
        m_priv->variables_editor.reset
            (new LocalVarsInspector (debugger (),
                                     *m_priv->workbench,
                                     *this));
    }
    THROW_IF_FAIL (m_priv->variables_editor);
    return *m_priv->variables_editor;
}

Gtk::ScrolledWindow&
DBGPerspective::get_local_vars_inspector_scrolled_win ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->variables_editor_scrolled_win) {
        m_priv->variables_editor_scrolled_win.reset (new Gtk::ScrolledWindow);
        m_priv->variables_editor_scrolled_win->set_policy
                            (Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    }
    THROW_IF_FAIL (m_priv->variables_editor_scrolled_win);
    return *m_priv->variables_editor_scrolled_win;
}


Terminal&
DBGPerspective::get_terminal ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->terminal) {
        string relative_path = Glib::build_filename ("menus",
                                                     "terminalmenu.xml");
        string absolute_path;
        THROW_IF_FAIL (build_absolute_resource_path
                (Glib::filename_to_utf8 (relative_path), absolute_path));

        m_priv->terminal.reset(new Terminal
                (absolute_path, workbench ().get_ui_manager ()));
    }
    THROW_IF_FAIL (m_priv->terminal);
    return *m_priv->terminal;
}

Gtk::Box &
DBGPerspective::get_terminal_box ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->terminal_box) {
        m_priv->terminal_box.reset (new Gtk::HBox);
        THROW_IF_FAIL (m_priv->terminal_box);
        Gtk::VScrollbar *scrollbar = Gtk::manage (new Gtk::VScrollbar);
        m_priv->terminal_box->pack_end (*scrollbar, false, false, 0);
        m_priv->terminal_box->pack_start (get_terminal ().widget ());
        scrollbar->set_adjustment (get_terminal ().adjustment ());
    }
    THROW_IF_FAIL (m_priv->terminal_box);
    return *m_priv->terminal_box;
}

/// Return the path name for the tty device that the debugging
/// perspective uses to communicate with the standard tty of the and
/// the inferior program.
///
/// If Nemiver is using the terminal from which it was launch (rather
/// than its own terminal widget), the name returned is the path name
/// for the tty device of that "launch terminal".
///
/// Note that this really is the path of the device created for the
/// slave side of the relevant pseudo terminal.
UString
DBGPerspective::get_terminal_name ()
{
    if (uses_launch_terminal () && isatty (0)) {
        return ttyname (0);
    } else {
        return get_terminal ().slave_pts_name ();
    }
}

Gtk::ScrolledWindow&
DBGPerspective::get_breakpoints_scrolled_win ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->breakpoints_scrolled_win) {
        m_priv->breakpoints_scrolled_win.reset (new Gtk::ScrolledWindow);
        THROW_IF_FAIL (m_priv->breakpoints_scrolled_win);
        m_priv->breakpoints_scrolled_win->set_policy (Gtk::POLICY_AUTOMATIC,
                                                      Gtk::POLICY_AUTOMATIC);
    }
    THROW_IF_FAIL (m_priv->breakpoints_scrolled_win);
    return *m_priv->breakpoints_scrolled_win;
}

BreakpointsView&
DBGPerspective::get_breakpoints_view ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->breakpoints_view) {
        m_priv->breakpoints_view.reset (new BreakpointsView (
                    workbench (), *this, debugger ()));
    }
    THROW_IF_FAIL (m_priv->breakpoints_view);
    return *m_priv->breakpoints_view;
}

Gtk::ScrolledWindow&
DBGPerspective::get_registers_scrolled_win ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->registers_scrolled_win) {
        m_priv->registers_scrolled_win.reset (new Gtk::ScrolledWindow);
        THROW_IF_FAIL (m_priv->registers_scrolled_win);
        m_priv->registers_scrolled_win->set_policy (Gtk::POLICY_AUTOMATIC,
                                                    Gtk::POLICY_AUTOMATIC);
    }
    THROW_IF_FAIL (m_priv->registers_scrolled_win);
    return *m_priv->registers_scrolled_win;
}

RegistersView&
DBGPerspective::get_registers_view ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->registers_view) {
        m_priv->registers_view.reset (new RegistersView (debugger ()));
    }
    THROW_IF_FAIL (m_priv->registers_view);
    return *m_priv->registers_view;
}

#ifdef WITH_MEMORYVIEW
MemoryView&
DBGPerspective::get_memory_view ()
{
    THROW_IF_FAIL (m_priv);
    if (!m_priv->memory_view) {
        m_priv->memory_view.reset (new MemoryView (debugger ()));
    }
    THROW_IF_FAIL (m_priv->memory_view);
    return *m_priv->memory_view;
}
#endif // WITH_MEMORYVIEW

/// Return the variable monitor view.
ExprMonitor&
DBGPerspective::get_expr_monitor_view ()
{
    THROW_IF_FAIL (m_priv);

    if (!m_priv->expr_monitor)
        m_priv->expr_monitor.reset (new ExprMonitor (*debugger (),
                                                     *this));
    THROW_IF_FAIL (m_priv->expr_monitor);
    return *m_priv->expr_monitor;
}

struct ScrollTextViewToEndClosure {
    Gtk::TextView* text_view;

    ScrollTextViewToEndClosure (Gtk::TextView *a_view=0) :
        text_view (a_view)
    {
    }

    bool do_exec ()
    {
        if (!text_view) {return false;}
        if (!text_view->get_buffer ()) {return false;}

        Gtk::TextIter end_iter = text_view->get_buffer ()->end ();
        text_view->scroll_to (end_iter);
        return false;
    }
};//end struct ScrollTextViewToEndClosure

sigc::signal<void, bool>&
DBGPerspective::activated_signal ()
{
    CHECK_P_INIT;
    return m_priv->activated_signal;
}

sigc::signal<void, IDebugger::State>&
DBGPerspective::attached_to_target_signal ()
{
    return m_priv->attached_to_target_signal;
}

sigc::signal<void>&
DBGPerspective::layout_changed_signal ()
{
    THROW_IF_FAIL (m_priv);
    return m_priv->layout_mgr.layout_changed_signal ();
}

sigc::signal<void, bool>&
DBGPerspective::going_to_run_target_signal ()
{
    return m_priv->going_to_run_target_signal;
}

sigc::signal<void>&
DBGPerspective::default_config_read_signal ()
{
    return m_priv->default_config_read_signal;
}

bool
DBGPerspective::agree_to_shutdown ()
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;
    if (debugger ()->is_attached_to_target ()) {
        UString message;
        message.printf (_("There is a program being currently debugged. "
                          "Do you really want to exit from the debugger?"));
        if (nemiver::ui_utils::ask_yes_no_question (message) ==
            Gtk::RESPONSE_YES) {
            return true;
        } else {
            return false;
        }
    } else {
        return true;
    }
}

class DBGPerspectiveModule : DynamicModule {

public:

    void get_info (Info &a_info) const
    {
        static Info s_info ("Debugger perspective plugin",
                            "The debugger perspective of Nemiver",
                            "1.0");
        a_info = s_info;
    }

    void do_init ()
    {
    }

    bool lookup_interface (const std::string &a_iface_name,
                           DynModIfaceSafePtr &a_iface)
    {
        LOG_DD ("looking up interface: " + a_iface_name);
        if (a_iface_name == "IPerspective") {
            a_iface.reset (new DBGPerspective (this));
        } else if (a_iface_name == "IDBGPerspective") {
            a_iface.reset (new DBGPerspective (this));
        } else {
            return false;
        }
        LOG_DD ("interface " + a_iface_name + " found");
        return true;
    }
};// end class DBGPerspective

NEMIVER_END_NAMESPACE (nemiver)

//the dynmod initial factory.
extern "C" {
bool
NEMIVER_API nemiver_common_create_dynamic_module_instance (void **a_new_instance)
{
    Gsv::init ();
    *a_new_instance = new nemiver::DBGPerspectiveModule ();
    return (*a_new_instance != 0);
}

}//end extern C
