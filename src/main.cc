//Author: Dodji Seketeli
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
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <iostream>
#include <gtkmm/window.h>
#include <glib/gi18n.h>
#include "nmv-str-utils.h"
#include "nmv-exception.h"
#include "nmv-initializer.h"
#include "nmv-i-workbench.h"
#include "nmv-ui-utils.h"
#include "nmv-proc-mgr.h"
#include "nmv-env.h"
#include "nmv-dbg-perspective.h"
#include "nmv-i-conf-mgr.h"

using namespace std;
using nemiver::IConfMgr;
using nemiver::common::DynamicModuleManager;
using nemiver::common::Initializer;
using nemiver::IWorkbench;
using nemiver::IWorkbenchSafePtr;
using nemiver::IDBGPerspective;
using nemiver::common::UString;
using nemiver::common::GCharSafePtr;
using nemiver::ISessMgr;

static const UString DBGPERSPECTIVE_PLUGIN_NAME = "dbgperspective";
static gchar *gv_env_vars = 0;
static gchar *gv_process_to_attach_to = 0;
static bool gv_list_sessions = false;
static bool gv_purge_sessions = false;
static int gv_execute_session = 0;
static bool gv_last_session = false;
static gchar *gv_log_domains=0;
static bool gv_log_debugger_output = false;
static bool gv_show_version = false;
static bool gv_use_launch_terminal = false;
static gchar *gv_remote = 0;
static gchar *gv_solib_prefix = 0;
static gchar *gv_gdb_binary_filepath = 0;
static gchar *gv_core_path = 0;
static bool gv_just_load = false;

static GOptionEntry entries[] =
{
    {
        "env",
        0,
        0,
        G_OPTION_ARG_STRING,
        &gv_env_vars,
        _("Set the environment of the program to debug"),
        "<\"var0=val0 var1=val1 var2=val2 ...\">"
    },
    {
        "attach",
        0,
        0,
        G_OPTION_ARG_STRING,
        &gv_process_to_attach_to,
        _("Attach to a process"),
        "<pid|process name>"
    },
    {
        "load-core",
        0,
        0,
        G_OPTION_ARG_STRING,
        &gv_core_path,
        _("Load a core file"),
        "</path/to/core/file>"
    },
    { "list-sessions",
      0,
      0,
      G_OPTION_ARG_NONE,
      &gv_list_sessions,
      _("List the saved debugging sessions"),
      0
    },
    { "purge-sessions",
      0,
      0,
      G_OPTION_ARG_NONE,
      &gv_purge_sessions,
      _("Erase the saved debugging sessions"),
      0
    },
    { "session",
      0,
      0,
      G_OPTION_ARG_INT,
      &gv_execute_session,
      _("Debug the program that was of session number N"),
      "<N>"
    },
    { "last",
      0,
      0,
      G_OPTION_ARG_NONE,
      &gv_last_session,
      _("Execute the last session"),
      0
    },
    { "log-domains",
      0,
      0,
      G_OPTION_ARG_STRING,
      &gv_log_domains,
      _("Enable logging domains DOMAINS"),
      "<DOMAINS>"
    },
    { "log-debugger-output",
      0,
      0,
      G_OPTION_ARG_NONE,
      &gv_log_debugger_output,
      _("Log the debugger output"),
      0
    },
    { "use-launch-terminal",
      0,
      0,
      G_OPTION_ARG_NONE,
      &gv_use_launch_terminal,
      _("Use this terminal as the debugee's terminal"),
      0
    },
    {
        "remote",
        0,
        0,
        G_OPTION_ARG_STRING,
        &gv_remote,
        _("Connect to remote target specified by HOST:PORT"),
        "<HOST:PORT|serial-line-path>"
    },
    {
        "solib-prefix",
        0,
        0,
        G_OPTION_ARG_STRING,
        &gv_solib_prefix,
        _("Where to look for shared libraries loaded by the inferior. "
          "Use in conjunction with --remote"),
        "</path/to/prefix>"
    },
    {
        "gdb-binary",
        0,
        0,
        G_OPTION_ARG_STRING,
        &gv_gdb_binary_filepath,
        _("Set the path of the GDB binary to use to debug the inferior"),
        "</path/to/gdb>"
    },
    {
        "just-load",
        0,
        0,
        G_OPTION_ARG_NONE,
        &gv_just_load,
        _("Do not set a breakpoint in 'main' and do not run the inferior either"),
        0
    },
    { 
        "version",
        0,
        0,
        G_OPTION_ARG_NONE,
        &gv_show_version,
        _("Show the version number of Nemiver"),
        0
    },
    {0, 0, 0, (GOptionArg) 0, 0, 0, 0}
};

struct GOptionContextUnref {
    void operator () (GOptionContext *a_opt)
    {
        if (a_opt) {
            g_option_context_free (a_opt);
        }
    }
};//end struct GOptionContextUnref

struct GOptionGroupUnref {
    void operator () (GOptionGroup *a_group)
    {
        if (a_group) {
            g_option_group_free (a_group);
        }
    }
};//end struct GOptionGroupUnref

struct GOptionContextRef {
    void operator () (GOptionContext *a_opt)
    {
        if (a_opt) {}
    }
};//end struct GOptionContextRef

struct GOptionGroupRef {
    void operator () (GOptionGroup *a_group)
    {
        if (a_group) {}
    }
};//end struct GOptionGroupRef

static IWorkbench *s_workbench=0;

void
sigint_handler (int a_signum)
{
    if (a_signum != SIGINT) {
        return;
    }
    static bool s_got_down = false;
    if (!s_got_down && s_workbench) {
        s_workbench->shut_down ();
        s_workbench = 0;
        s_got_down = true;
    }
}
typedef SafePtr<GOptionContext,
                GOptionContextRef,
                GOptionContextUnref> GOptionContextSafePtr;

typedef SafePtr<GOptionGroup,
                GOptionGroupRef,
                GOptionGroupUnref> GOptionGroupSafePtr;

static GOptionContext*
init_option_context ()
{
    GOptionContextSafePtr context;
    context.reset (g_option_context_new
                                (_(" [<prog-to-debug> [prog-args]]")));
#if GLIB_CHECK_VERSION (2, 12, 0)
    g_option_context_set_summary (context.get (),
                                  _("A C/C++ debugger for GNOME"));
#endif
    g_option_context_set_help_enabled (context.get (), true);
    g_option_context_add_main_entries (context.get (),
                                       entries,
                                       GETTEXT_PACKAGE);
    g_option_context_set_ignore_unknown_options (context.get (), false);
    GOptionGroupSafePtr gtk_option_group (gtk_get_option_group (FALSE));
    THROW_IF_FAIL (gtk_option_group);
    g_option_context_add_group (context.get (),
                                gtk_option_group.release ());
    return context.release ();
}

/// Parse the command line and edits it
/// to make it contain the command line of the inferior program.
/// If an error happens (e.g, the user provided the wrong command
/// lines) then display an usage help message and return
/// false. Otherwise, return true.
/// \param a_arg the argument count. This is the length of a_argv.
///  This is going to be edited. After edit, only the number of
///  arguments of the inferior will be put in this variable.
/// \param a_argv the string of arguments passed to Nemiver. This is
/// going to be edited so that only the arguments passed to the
/// inferior will be left in this.
/// \return true upon successful completion, false otherwise. If the
static bool
parse_command_line (int& a_argc,
                    char** a_argv)
{
    GOptionContextSafePtr context (init_option_context ());
    THROW_IF_FAIL (context);

    if (a_argc == 1) {
        // We have no inferior program so edit the command like accordingly.
        a_argc = 0;
        a_argv[0] = 0;
        return true;
    }

    // Split the command line in two parts. One part is made of the
    // options for Nemiver itself, and the other part is the options
    // relevant to the inferior.
    int i;
    std::vector<UString> args;
    for (i = 1; i < a_argc; ++i)
        if (a_argv[i][0] != '-')
            break;

    // Now parse only the part of the command line that is related
    // to Nemiver and not to the inferior.
    // Once parsed, make a_argv and a_argv contain the command line
    // of the inferior.
    char **nmv_argv, **inf_argv;
    int nmv_argc = a_argc;
    int inf_argc = 0;

    if (i < a_argc) {
        nmv_argc = i;
        inf_argc = a_argc - i;
    }
    nmv_argv = a_argv;
    inf_argv = a_argv + i;
    GError *error = 0;
    if (g_option_context_parse (context.get (),
                                &nmv_argc,
                                &nmv_argv,
                                &error) != TRUE) {
        NEMIVER_TRY;
        if (error)
            cerr << "error: "<< error->message << std::endl;
        NEMIVER_CATCH;
        g_error_free (error);

        GCharSafePtr help_message;
        help_message.reset (g_option_context_get_help (context.get (),
                                                       true, 0));
        cerr << help_message.get () << std::endl;
        return false;
    }

    if (a_argv != inf_argv) {
        memmove (a_argv, inf_argv, inf_argc * sizeof (char*));
        a_argc = inf_argc;
    }

    //***************************************************************
    // Here goes some sanity checking on the command line parsed so far.
    //****************************************************************

    // If the user wants to debug a binary running on a remote target,
    // make sure she provides us with a local copy of the binary too.
    if (gv_remote) {
        if (a_argc < 1 || a_argv[0][0] == '-') {
            cerr << _("Please provide a local copy of the binary "
                      "you intend to debug remotely.\n"
                      "Like this:\n")
                 << "nemiver --remote=" << gv_remote
                 << " <binary-copy>\n\n";
            cerr << _("Otherwise, find below the full set of Nemiver options.\n");
            GCharSafePtr help_message;
            help_message.reset (g_option_context_get_help (context.get (),
                                                           true, 0));
            cerr << help_message.get () << std::endl;
            return false;
        }
    }

    // If the user wants to analyse a core dump, make sure she
    // provides us with a path to the binary that generated the core
    // dump too.
    if (gv_core_path) {
        if (a_argc < 1 || a_argv[0][0] == '-') {
            cerr << _("Please provide the path to the binary "
                      "that generated the core file too.\n"
                      "Like this:\n")
                 << "nemiver --load-core=\""
                 << gv_core_path << "\" </path/to/binary>\n\n";
            cerr << _("Otherwise, find below the full set of Nemiver options.\n");
            GCharSafePtr help_message;
            help_message.reset (g_option_context_get_help (context.get (),
                                                           true, 0));
            cerr << help_message.get () << std::endl;
            return false;
        }
    }
    return true;
}

// Return true if Nemiver should keep going after the non gui options
// have been processed.
static bool
process_non_gui_options ()
{
    if (gv_log_debugger_output) {
        LOG_STREAM.enable_domain ("gdbmi-output-domain");
    }

    if (gv_show_version) {
        cout << PACKAGE_VERSION << endl;
        return false;
    }

    if (gv_log_domains) {
        UString log_domains (gv_log_domains);
        vector<UString> domains = log_domains.split (" ");
        for (vector<UString>::const_iterator iter = domains.begin ();
             iter != domains.end ();
             ++iter) {
            LOG_STREAM.enable_domain (*iter);
        }
    }
    return true;
}

/// Load the debugger perspective.
static IDBGPerspective*
load_debugger_perspective ()
{
    return dynamic_cast<IDBGPerspective*> (s_workbench->get_perspective
                                           (DBGPERSPECTIVE_PLUGIN_NAME));
}

/// Return true if Nemiver should keep going after the GUI option(s)
/// have been processed.
static bool
process_gui_options (int& a_argc, char** a_argv)
{
    if (gv_purge_sessions) {
        IDBGPerspective *debug_persp = load_debugger_perspective ();
        if (debug_persp) {
            debug_persp->session_manager ().delete_sessions ();
        }
        return false;
    }

    if (gv_process_to_attach_to) {
        using nemiver::common::IProcMgrSafePtr;
        using nemiver::common::IProcMgr;
        IDBGPerspective *debug_persp = load_debugger_perspective ();
        if (!debug_persp) {
            cerr << "Could not get the debugging perspective" << endl;
            return false;
        }
        int pid = atoi (gv_process_to_attach_to);
        if (!pid) {
            IProcMgrSafePtr proc_mgr = IProcMgr::create ();
            if (!proc_mgr) {
                cerr << "Could not create proc mgr" << endl;
                return false;
            }
            IProcMgr::Process process;
            if (!proc_mgr->get_process_from_name (gv_process_to_attach_to,
                                                  process, true)) {
                cerr << "Could not find any process named '"
                << gv_process_to_attach_to
                << "'"
                << endl;
                return false;
            }
            pid = process.pid ();
        }
        if (!pid) {
            cerr << "Could not find any process '"
                 << gv_process_to_attach_to
                 << "'"
                 << endl;
            return false;
        } else {
            debug_persp->uses_launch_terminal (gv_use_launch_terminal);
            debug_persp->attach_to_program (pid);
        }
    }

    if (gv_list_sessions) {        
        IDBGPerspective *debug_persp = load_debugger_perspective ();
        if (debug_persp) {
            debug_persp->session_manager ().load_sessions ();
            list<ISessMgr::Session>::iterator session_iter;
            list<ISessMgr::Session>& sessions =
                            debug_persp->session_manager ().sessions ();
            for (session_iter = sessions.begin ();
                 session_iter != sessions.end ();
                 ++session_iter) {
                cout << session_iter->session_id ()
                     << " "
                     << session_iter->properties ()["sessionname"]
                     << "\n";
            }
            return false;
        } else {
            cerr << "Could not find the debugger perpective plugin";
            return false;
        }
    }

    if (gv_execute_session) {
        IDBGPerspective *debug_persp = load_debugger_perspective ();
        if (debug_persp) {
            debug_persp->session_manager ().load_sessions ();
            list<ISessMgr::Session>::iterator session_iter;
            list<ISessMgr::Session>& sessions =
                            debug_persp->session_manager ().sessions ();
            bool found_session=false;
            debug_persp->uses_launch_terminal (gv_use_launch_terminal);
            for (session_iter = sessions.begin ();
                 session_iter != sessions.end ();
                 ++session_iter) {
                if (session_iter->session_id () == gv_execute_session) {
                    debug_persp->execute_session (*session_iter);
                    found_session = true;
                    break;
                }
            }

            if (!found_session) {
                cerr << "Could not find session of number "
                     << gv_execute_session
                     << "\n";
                return false;
            }
            return true;
        } else {
            cerr << "Could not find the debugger perpective plugin";
            return false;
        }
    }

    //execute the last session if one exists
    if (gv_last_session) {
        IDBGPerspective *debug_persp = load_debugger_perspective ();
        if (debug_persp) {
            debug_persp->session_manager ().load_sessions ();
            list<ISessMgr::Session>& sessions =
                            debug_persp->session_manager ().sessions ();
            if (!sessions.empty ()) {
                debug_persp->uses_launch_terminal (gv_use_launch_terminal);
                list<ISessMgr::Session>::iterator session_iter,
                    latest_session_iter;
                glong time_val = 0;
                for (session_iter = sessions.begin ();
                        session_iter != sessions.end ();
                        ++session_iter) {
                    std::map<UString, UString>::const_iterator map_iter =
                                session_iter->properties ().find ("lastruntime");
                    if (map_iter != session_iter->properties ().end ()) {
                        glong new_time = atoi (map_iter->second.c_str ());
                        if (new_time > time_val) {
                            time_val = new_time;
                            latest_session_iter = session_iter;
                        }
                    }
                }
                debug_persp->execute_session (*latest_session_iter);
            } else {
                cerr << "Could not find any sessions"
                     << "\n";
                return false;
            }
            return true;
        } else {
            cerr << "Could not find the debugger perpective plugin";
            return false;
        }
    }

    // Load and analyse a core file
    if (gv_core_path) {
        IDBGPerspective *debug_persp = load_debugger_perspective ();
        if (debug_persp == 0) {
            cerr << "Could not find the debugger perpective plugin";
            return false;
        }
        UString prog_path = a_argv[0];
        THROW_IF_FAIL (!prog_path.empty ());
        debug_persp->load_core_file (prog_path, gv_core_path);
        return true;
    }

    vector<UString> prog_args;
    UString prog_path;
    // Here, a_argc is the argument count of the inferior program.
    // It's zero if there is there is no inferior program.
    // Otherwise it equals the number of arguments to the inferior program + 1
    if (a_argc > 0)
        prog_path = a_argv[0];
    for (int i = 1; i < a_argc; ++i) {
        prog_args.push_back (Glib::locale_to_utf8 (a_argv[i]));
    }
    IDBGPerspective *debug_persp =
        dynamic_cast<IDBGPerspective*> (s_workbench->get_perspective
                                            (DBGPERSPECTIVE_PLUGIN_NAME));
    if (debug_persp) {
        if (gv_gdb_binary_filepath) {
            char *debugger_full_path = realpath (gv_gdb_binary_filepath, 0);
            if (debugger_full_path) {
                nemiver::IDebuggerSafePtr debugger = debug_persp->debugger ();
                if (!debugger) {
                    cerr << "Could not get the debugger instance" << endl;
                    return false;
                }
                debugger->set_non_persistent_debugger_path (debugger_full_path);
                free (debugger_full_path);
            } else {
                LOG_ERROR ("Could not resolve the full path of the debugger");
            }
        }

        map<UString, UString> env;
        if (gv_env_vars) {
            vector<UString> env_vars =
                UString (Glib::locale_to_utf8 (gv_env_vars)).split (" ");
            for (vector<UString>::const_iterator it = env_vars.begin ();
                 it != env_vars.end ();
                 ++it) {
                vector<UString> env_var = it->split ("=");
                if (env_var.size () != 2) {
                    continue;
                }
                UString name = env_var[0];
                name.chomp ();
                UString value = env_var[1];
                value.chomp ();
                LOG_DD ("got env var: " << name << "=" << value);
                env[name] = value;
            }
        }
	if (gv_remote) {
            // The user asked to connect to a remote target.
            std::string host;
            unsigned port = 0;
            std::string solib_prefix;
			
            if (gv_solib_prefix)
                solib_prefix = gv_solib_prefix;

            if (nemiver::str_utils::parse_host_and_port (gv_remote,
                                                         host, port)) {
                // So it looks like gv_remote has the form
                // "host:port", so let's try to connect to that.
                debug_persp->connect_to_remote_target (host, port,
                                                       prog_path,
                                                       solib_prefix);
			}
			else {
                // We think gv_remote contains a serial line address.
                // Let's try to connect via the serial line then.
                debug_persp->connect_to_remote_target (gv_remote,
                                                       prog_path,
                                                       solib_prefix);
			}
	}
	 else if (!prog_path.empty ()) {
            // The user wants to debug a local program
            debug_persp->uses_launch_terminal (gv_use_launch_terminal);
            debug_persp->execute_program (prog_path,
                                          prog_args,
                                          env,
                                          /*a_cwd=*/".",
                                          /*a_clone_opened_files=*/false,
                                          /*a_break_in_main_run=*/!gv_just_load);
        }
    } else {
        cerr << "Could not find the debugger perspective plugin\n";
        return false;
    }
    return true;
}

int
main (int a_argc, char *a_argv[])
{
    bindtextdomain (GETTEXT_PACKAGE, NEMIVERLOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);

    NEMIVER_TRY

    Initializer::do_init ();
    Gtk::Main gtk_kit (a_argc, a_argv);

    if (parse_command_line (a_argc, a_argv) == false)
        return -1;

    if (process_non_gui_options () != true) {
        return -1;
    }

    //********************************************
    //load and init the workbench dynamic module
    //********************************************
    IWorkbenchSafePtr workbench =
        nemiver::load_iface_and_confmgr<IWorkbench> ("workbench",
                                                     "IWorkbench");
    s_workbench = workbench.get ();
    THROW_IF_FAIL (s_workbench);
    LOG_D ("workbench refcount: " <<  (int) s_workbench->get_refcount (),
           "refcount-domain");

    s_workbench->do_init (gtk_kit);
    LOG_D ("workbench refcount: " <<  (int) s_workbench->get_refcount (),
           "refcount-domain");

    if (process_gui_options (a_argc, a_argv) != true) {
        return -1;
    }

    //intercept ctrl-c/SIGINT
    signal (SIGINT, sigint_handler);

    if (gv_use_launch_terminal) {
        // So the user wants the inferior to use the terminal Nemiver was
        // launched from, for input/output.
        //
        // Letting the inferior use the terminal from which Nemiver was
        // launched from implies calling the "set inferior-tty" GDB command,
        // when we are using the GDB backend.
        // That command is implemented using the TIOCSCTTY ioctl. It sets the
        // terminal as the controlling terminal of the inferior process. But that
        // ioctl requires (among other things) that the terminal shall _not_ be
        // the controlling terminal of any other process in another process
        // session already. Otherwise, GDB spits the error:
        // "GDB: Failed to set controlling terminal: operation not
        // permitted"
        // The problem is, Nemiver itself uses that terminal as a
        // controlling terminal. So Nemiver itself must relinquish its
        // controlling terminal to avoid letting the ioctl fail.
        // We do so by calling the setsid function below.
        // The problem is that setsid will fail with EPERM if Nemiver is
        // started as a process group leader already.
        // Trying ioctl (tty_fd, TIOCNOTTY, 0) does not seem to properly
        // disconnect Nemiver from its terminal either. Sigh.
        setsid ();
    }

    gtk_kit.run (s_workbench->get_root_window ());

    NEMIVER_CATCH_NOX
    s_workbench = 0;

    return 0;
}

