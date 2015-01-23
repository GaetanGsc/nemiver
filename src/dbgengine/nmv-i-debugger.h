// -*- c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 4; -*-'
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
#ifndef __NMV_I_DEBUGGER_H__
#define __NMV_I_DEBUGGER_H__

#include <stdint.h>
#include <vector>
#include <string>
#include <map>
#include <list>
#include "common/nmv-api-macros.h"
#include "common/nmv-ustring.h"
#include "common/nmv-dynamic-module.h"
#include "common/nmv-safe-ptr-utils.h"
#include "common/nmv-address.h"
#include "common/nmv-asm-instr.h"
#include "common/nmv-loc.h"
#include "common/nmv-str-utils.h"
#include "nmv-i-conf-mgr.h"

using nemiver::common::SafePtr;
using nemiver::common::DynamicModule;
using nemiver::common::DynamicModuleSafePtr;
using nemiver::common::DynModIface;
using nemiver::common::ObjectRef;
using nemiver::common::ObjectUnref;
using nemiver::common::UString;
using nemiver::common::Object;
using nemiver::common::Address;
using nemiver::common::AsmInstr;
using nemiver::common::MixedAsmInstr;
using nemiver::common::Asm;
using nemiver::common::DisassembleInfo;
using nemiver::common::Loc;
using std::vector;
using std::string;
using std::map;
using std::list;

NEMIVER_BEGIN_NAMESPACE (nemiver)

class ILangTrait;
class IDebugger;
typedef SafePtr<IDebugger, ObjectRef, ObjectUnref> IDebuggerSafePtr;

/// \brief a debugger engine.
///
///It should abstract the real debugger used underneath, but
///it is modeled after the interfaces exposed by gdb.
///Please, read GDB/MI interface documentation for more.
class NEMIVER_API IDebugger : public DynModIface {

    IDebugger (const IDebugger&);
    IDebugger& operator= (const IDebugger&);

protected:

    IDebugger (DynamicModule *a_dynmod) : DynModIface (a_dynmod)
    {
    }

public:

    typedef unsigned int register_id_t;

    /// \brief a breakpoint descriptor
    class Breakpoint {
    public:

        enum Type {
            UNDEFINED_TYPE = 0,
            STANDARD_BREAKPOINT_TYPE,
            WATCHPOINT_TYPE,
            COUNTPOINT_TYPE
        };

    private:
        int m_number;
        bool m_enabled;
        Address m_address;
        string m_function;
        string m_expression;
        UString m_file_name;
        UString m_file_full_name;
        string m_condition;
        Type m_type;
        int m_line;
        int m_nb_times_hit;
        // The ignore count set by the user.
        int m_initial_ignore_count;
        // The current ignore count.  This one is decremented each
        // time the breakpoint is hit.
        int m_ignore_count;
        bool m_is_read_watchpoint;
        bool m_is_write_watchpoint;
        // The list of sub-breakpoints, in case this breakpoint is a
        // multiple breakpoint.  In that case, each sub-breakpoint
        // will be set to a real location of an overload function.
        // Each sub-breakpoint will then have a non-nil
        // m_parent_breakpoint pointer that points back to this
        // instance, and an empty m_sub_breakpoints member.
        vector<Breakpoint> m_sub_breakpoints;
        // If this instance is a sub-breakpoint, i.e, a breakpoint to
        // an overloaded function that has been set by name to all the
        // overloads at the same time, then this is set to the id of
        // the parent breakpoint.  The parent breakpoint contains
        // information that are relevant for all the sub-breakpoints.
        int m_parent_breakpoint_number;
        // Whether the breakpoint is pending.
        bool m_is_pending;

    public:
        Breakpoint () {clear ();}

        /// \name accessors

        /// @{

        /// Getter of the ID of this breakpoint.  If the breakpoint is
        /// a sub breakpoint, its id has the form of the string "5.4"
        /// where 5 is the number of the parent breakpoint and 4 is
        /// the number of this sub breakpoint.
        ///
        /// @return the ID this breakpoint.
        string id () const
        {
            if (is_sub_breakpoint ())
                return (str_utils::int_to_string (parent_breakpoint_number ()) + "."
                        + str_utils::int_to_string (sub_breakpoint_number ()));
            else
                return str_utils::int_to_string (sub_breakpoint_number ());
        }

        /// Getter of the ID of the parent of this breakpoint.
        ///
        ///Note that (at least for GDB) only parent breakpoints can be
        ///deleted by ID
        ///
        ///@return the ID of the parent of this breakpoint.
        string parent_id () const
        {
            string id;
            if (is_sub_breakpoint ())
                id = str_utils::int_to_string(parent_breakpoint_number ());
            else
                id = str_utils::int_to_string (sub_breakpoint_number ());
            return id;
        }

        /// Getter of the ID of this breakpoint.  If the breakpoint is
        /// a sub breakpoint, its id has the form of the string "5.4"
        /// where 5 is the number of the parent breakpoint and 4 is
        /// the number of this sub breakpoint.
        ///
        /// @param s out parameter. Set to the ID of this breakpoint.
        void id (string& s) const
        {
            s = number ();
        }

        /// Getter of the number of this breakpoint.  Note that if
        /// this is a sub breakpoint, what you might want is the
        /// number of the parent breakpoint.
        ///
        /// @return the integer number of this breakpoint.
        int number() const {return sub_breakpoint_number();}

        void number (int a_in) {m_number = a_in;}
        int sub_breakpoint_number () const {return m_number;}

        bool enabled () const {return m_enabled;}
        void enabled (bool a_in) {m_enabled = a_in;}

        const Address& address () const {return m_address;}
        Address& address () {return m_address;}
        void address (const string &a_in)
        {
            m_address = a_in;
            if (!m_address.empty ())
                m_is_pending = false;
        }

        const string& function () const {return m_function;}
        void function (const string &a_in) {m_function = a_in;}

        const string& expression () const {return m_expression;}
        void expression (const string &a_expr) {m_expression = a_expr;}

        const UString& file_name () const {return m_file_name;}
        void file_name (const UString &a_in) {m_file_name = a_in;}

        const UString& file_full_name () const {return m_file_full_name;}
        void file_full_name (const UString &a_in) {m_file_full_name = a_in;}

        int line () const {return m_line;}
        void line (int a_in) {m_line = a_in;}

        const string& condition () const {return m_condition;}
        void condition (const string &a_cond) {m_condition = a_cond;}

        bool has_condition () const {return !m_condition.empty ();}

        int nb_times_hit () const {return m_nb_times_hit;}
        void nb_times_hit (int a_nb) {m_nb_times_hit = a_nb;}

        ///  This is the ignore count as set by the user through the
        ///  user interface.
        int initial_ignore_count () const {return m_initial_ignore_count;}
        void initial_ignore_count (int a) {m_initial_ignore_count = a;}

        /// This is the current ignore count present on the
        /// breakpoint.  It gets decremented each time the breakpoint
        /// is hit.
        int ignore_count () const {return m_ignore_count;}
        void ignore_count (int a) {m_ignore_count = a;}

        bool is_read_watchpoint () const {return m_is_read_watchpoint;}
        void is_read_watchpoint (bool f) {m_is_read_watchpoint = f;}

        bool is_write_watchpoint () const {return m_is_write_watchpoint;}
        void is_write_watchpoint (bool f) {m_is_write_watchpoint = f;}

        bool is_pending () const {return m_is_pending;}
        void is_pending (bool a) {m_is_pending = a;}

        /// Test whether this breakpoint has multiple location.
        ///
        /// Each location is then represented by a sub-breakpoint,
        /// that is a child breakpoint of this instance of
        /// breakpoint.  The sub-breakpoints can be retrieved by the
        /// #sub_breakpoints below.
        bool has_multiple_locations () const {return !m_sub_breakpoints.empty ();}

        void append_sub_breakpoint (Breakpoint& b)
        {
            b.m_parent_breakpoint_number = m_number;
            m_sub_breakpoints.push_back (b);
        }

        const vector<Breakpoint>& sub_breakpoints () const
        {
            return m_sub_breakpoints;
        }

        int parent_breakpoint_number () const {return m_parent_breakpoint_number;}

        bool is_sub_breakpoint () const {return !!parent_breakpoint_number ();}

        Type type () const {return m_type;}
        void type (Type a_type) {m_type = a_type;}

        /// @}

        /// \brief clear this instance of breakpoint
        void clear ()
        {
            m_type = STANDARD_BREAKPOINT_TYPE;
            m_number = 0;
            m_enabled = false;
            m_address.clear ();
            m_function.clear ();
            m_file_name.clear ();
            m_file_full_name.clear ();
            m_line = 0;
            m_condition.clear ();
            m_nb_times_hit = 0;
            m_initial_ignore_count = 0;
            m_ignore_count = 0;
            m_is_read_watchpoint = false;
            m_is_write_watchpoint = false;
            m_sub_breakpoints.clear ();
            m_parent_breakpoint_number = 0;
            m_is_pending = false;
        }
    };//end class Breakpoint

    /// \brief an entry of a choice list
    /// to choose between a list of overloaded
    /// functions. This is used for instance
    /// to propose a choice between several
    /// overloaded functions when the user asked
    /// to break into a function by name and when
    /// that name has several overloads.
    class OverloadsChoiceEntry {
    public:
        enum Kind {
            CANCEL=0,
            ALL,
            LOCATION
        };

    private:
        Kind m_kind;
        int m_index;
        UString m_function_name;
        UString m_file_name;
        int m_line_number;

        void init (Kind a_kind, int a_index,
                   const UString &a_function_name,
                   const UString &a_file_name,
                   int a_line_number)
        {
            kind (a_kind);
            index (a_index);
            function_name (a_function_name);
            file_name (a_file_name);
            line_number (a_line_number);
        }

    public:
        OverloadsChoiceEntry (Kind a_kind,
                              int a_index,
                              UString &a_function_name,
                              UString a_file_name,
                              int a_line_number)
        {
            init (a_kind, a_index, a_function_name,
                  a_file_name, a_line_number);
        }

        OverloadsChoiceEntry ()
        {
            init (CANCEL, 0, "", "", 0);
        }

        Kind kind () const {return m_kind;}
        void kind (Kind a_kind) {m_kind = a_kind;}

        int index () const {return m_index;}
        void index (int a_index) {m_index = a_index;}

        const UString& function_name () const {return m_function_name;}
        void function_name (const UString& a_in) {m_function_name = a_in;}

        const UString& file_name () const {return m_file_name;}
        void file_name (const UString &a_in) {m_file_name = a_in;}

        int line_number () const {return m_line_number;}
        void line_number (int a_in) {m_line_number = a_in;}
    };//end class OverloadsChoiceEntry
    typedef vector<OverloadsChoiceEntry> OverloadsChoiceEntries;

    class Variable;
    typedef SafePtr<Variable, ObjectRef, ObjectUnref> VariableSafePtr;
    typedef list<VariableSafePtr> VariableList;

    /// \brief a function frame as seen by the debugger.
    class Frame {
        Address m_address;
        string m_function_name;
        map<string, string> m_args;
        int m_level;
        //present if the target has debugging info
        UString m_file_name;
        //present if the target has sufficient debugging info
        UString m_file_full_name;
        int m_line;
        //present if the target doesn't have debugging info
        string m_library;
    public:

        Frame () :
        m_level (0),
            m_line (0)
            {
            }

        /// \operators
        /// @{

        bool operator== (const Frame &a) const
        {
            return (level () == a.level ()
                    && function_name () == a.function_name ()
                    && file_name () == a.file_name ()
                    && library () == a.library ());
        }

        bool operator!= (const Frame &a) const {return !(operator== (a));}

        /// @}
        /// \name accessors

        /// @{
        const Address& address () const {return m_address;}
        Address& address () {return m_address;}
        void address (const Address &a_in) {m_address = a_in;}
        bool has_empty_address () const
        {
            return m_address.to_string ().empty ();
        }

        const string& function_name () const {return m_function_name;}
        void function_name (const string &a_in) {m_function_name = a_in;}

        const map<string, string>& args () const {return m_args;}
        map<string, string>& args () {return m_args;}

        int level () const {return m_level;}
        void level (int a_level) {m_level = a_level;}

        const UString& file_name () const {return m_file_name;}
        void file_name (const UString &a_in) {m_file_name = a_in;}

        const UString& file_full_name () const {return m_file_full_name;}
        void file_full_name (const UString &a_in) {m_file_full_name = a_in;}

        int line () const {return m_line;}
        void line (int a_in) {m_line = a_in;}

        const string& library () const {return m_library;}
        void library (const string &a_library) {m_library = a_library;}

        /// @}

        /// \brief clears the current instance
        void clear ()
        {
            m_address = "";
            m_function_name = "";
            m_args.clear ();
            m_level = 0;
            m_file_name = "";
            m_file_full_name = "";
            m_line = 0;
            m_library.clear ();
            m_args.clear ();
        }
    };//end class Frame

    typedef sigc::slot<void> DefaultSlot;
    typedef sigc::slot<void, const vector<IDebugger::Frame>&>
        FrameVectorSlot;
    typedef sigc::slot<void, const map<int, IDebugger::VariableList>& >
        FrameArgsSlot;

    typedef sigc::slot<void, const VariableSafePtr> ConstVariableSlot;
    typedef sigc::slot<void, const VariableList&> ConstVariableListSlot;
    typedef sigc::slot<void, const UString&> ConstUStringSlot;

    class Variable : public Object {
    public:
        enum Format {
            UNDEFINED_FORMAT = 0,
            BINARY_FORMAT,
            DECIMAL_FORMAT,
            HEXADECIMAL_FORMAT,
            OCTAL_FORMAT,
            NATURAL_FORMAT,
            UNKNOWN_FORMAT // must be last
        };
    private:
        // non copyable.
        Variable (const Variable &);
        Variable& operator= (const Variable &);

        VariableList m_members;
        // If this variable was created with a backend counterpart
        // (e.g: backend side variable objects in GDB), then this
        // is the name of the backend side counterpart of this variable.
        UString m_internal_name;
        // If the variable was created with a backend counterpart
        // (e.g, GDB Variable objects), then this client-side
        // variable instance needs to have a hold on the instance of
        // IDebugger that was used to create the variable.  This is
        // needed so that that instance of IDebugger can be used to
        // tie the life cycle of the remote variable object peer with
        // the life cycle of this instance.
        mutable IDebugger *m_debugger;
        UString m_name;
        UString m_name_caption;
        UString m_value;
        UString m_type;
        // When using GDB pretty-printers, this is a string naming the
        // pretty printer used to visualize this variable. As
        // disabling pretty printing is not possible globally in GDB
        // we need to use this even when the user doesn't want to
        // change it.  Setting it to "" should do the right thing: if
        // pretty printing is enabled, the variable will be displayed
        // using  the default pretty printer; if pretty printing is
        // disabled the variable would be displayed using no pretty
        // printer.
        UString m_visualizer;
        UString m_display_hint;
        Variable *m_parent;
        //if this variable is a pointer,
        //it can be dereferenced. The variable
        //it points to is stored in m_dereferenced
        VariableSafePtr m_dereferenced;
        unsigned int m_num_expected_children;
        // The expression with which this variable
        // Can be referenced in the debugger.
        // If empty, it can be set by calling
        // IDebugger::query_variable_path_expr()
        UString m_path_expression;
        bool m_in_scope;
        Format m_format;
        bool m_needs_revisualizing;
        bool m_is_dynamic;
        bool m_has_more_children;

    public:
        explicit Variable (const UString &a_internal_name,
                           const UString &a_name,
                           const UString &a_value,
                           const UString &a_type,
                           bool a_in_scope = true,
                           IDebugger *a_dbg = 0)
            : m_internal_name (a_internal_name),
            m_debugger (a_dbg),
            m_name (a_name),
            m_value (a_value),
            m_type (a_type),
            m_parent (0),
            m_num_expected_children (0),
            m_in_scope (a_in_scope),
            m_format (UNDEFINED_FORMAT),
            m_needs_revisualizing (false),
            m_is_dynamic (false),
            m_has_more_children (false)
        {
        }

        explicit Variable (const UString &a_name,
                           const UString &a_value,
                           const UString &a_type,
                           bool a_in_scope = true,
                  IDebugger *a_dbg = 0)
            : m_debugger (a_dbg),
            m_name (a_name),
            m_value (a_value),
            m_type (a_type),
            m_parent (0),
            m_num_expected_children (0),
            m_in_scope (a_in_scope),
            m_format (UNDEFINED_FORMAT),
            m_needs_revisualizing (false),
            m_is_dynamic (false),
            m_has_more_children (false)

        {
        }

        explicit Variable (const UString &a_name,
                           IDebugger *a_dbg = 0)
            : m_debugger (a_dbg),
            m_name (a_name),
            m_parent (0),
            m_num_expected_children (0),
            m_in_scope (true),
            m_format (UNDEFINED_FORMAT),
            m_needs_revisualizing (false),
            m_is_dynamic (false),
            m_has_more_children (false)

        {
        }

        Variable (IDebugger *a_dbg = 0)
            : m_debugger (a_dbg),
            m_parent (0),
            m_num_expected_children (0),
            m_in_scope (true),
            m_format (UNDEFINED_FORMAT),
            m_needs_revisualizing (false),
                    m_is_dynamic (false),
            m_has_more_children (false)
        {
        }

        ~Variable ()
        {
            // If this variable is peered with an engine-side variable
            // object then ask the debugging engine to delete the peer
            // variable object now.
            if (m_debugger
                && !internal_name ().empty ()
                && m_debugger->is_attached_to_target ()) {
                IDebugger::DefaultSlot empty_slot;
                m_debugger->delete_variable (internal_name (),
                                             empty_slot);
            }
        }

        const VariableList& members () const {return m_members;}

        VariableList& members () {return m_members;}

        /// Returns the Nth member variable, if any.
        /// Please note that this function has O(n) complexity. So use it with
        /// care.
        /// \param a_index, the index of the member variable to return,
        ///        starting from zero.
        /// \param a_var the output parameter. Is set if and only if the
        ///         the function returned true.
        /// \return true if there was a variable at the specified index,
        ///  false otherwise.
        bool get_member_at (int a_index,
                            VariableSafePtr &a_var) const
        {
            int i = 0;
            for (VariableList::const_iterator it = members ().begin ();
                 it != m_members.end ();
                 ++it, ++i) {
                if (i == a_index) {
                    a_var = *it;
                    return true;
                }
            }
            return false;
        }

        /// \return the sibbling index of the variable node.
        /// If first sibbling at a given level has index 0.
        int sibling_index () const
        {
            if (!parent ())
                return 0;
            VariableList::const_iterator it;
            int i = 0;
            for (it = parent ()->members ().begin ();
                 it != parent ()->members ().end ();
                 ++it, ++i) {
                if (*it == this)
                    return i;
            }
            THROW ("fatal: should not be reached");
        }

        bool operator== (const Variable &a_other) const
        {
            return equals (a_other);
        }

        // Tests if this variable equals another one, by first
        // considering the variables' internal names, if they have
        // any.  Otherwise, tests if they are equal by value, i.e,
        // compare them memberwise.
        bool equals (const Variable &a_other) const
        {
            if (!internal_name ().empty ()
                && !a_other.internal_name ().empty ())
                return internal_name () == a_other.internal_name ();
            return equals_by_value (a_other);
        }

        /// Tests value equality between two variables.
        /// Two variables are considered equal by value if their
        /// respective members have the same values and same type.
        /// \param a_other the other variable to test against
        /// \return true if a_other equals the current instance
        ///         by value.
        bool equals_by_value (const Variable &a_other) const
        {
            if (name () != a_other.name ()
                || type () != a_other.type ())
                return false;
            if (members ().empty () != a_other.members ().empty ())
                return false;
            VariableList::const_iterator it0, it1;
            for (it0 = members ().begin (), it1 = a_other.members ().begin ();
                 it0 != members ().end ();
                 ++it0, ++it1) {
                if (it1 == a_other.members ().end ())
                    return false;
                if (!(*it0)->equals_by_value (**it1))
                    return false;
            }
            if (it1 != a_other.members ().end ())
                return false;
            return true;
        }

        void append (const VariableSafePtr &a_var)
        {
            if (!a_var) {return;}
            m_members.push_back (a_var);
            a_var->parent (this);
        }

        /// If this variable was created with a backend counterpart
        /// (e.g. backend side variable objects in GDB), then this
        /// returns the name of the backend side counterpart of this variable.
        /// \return the name of the backend side counterpart of this
        /// variable. It can be an empty string if there is no backend side
        /// counterpart variable object created for this object.
        const UString& internal_name () const {return m_internal_name;}

        /// Set the name of the backend side counterpart object.
        /// (e.g. variable object in GDB).
        /// \param a_in the new name of backend side counterpart variable object.
        void internal_name (const UString &a_in) {m_internal_name = a_in;}

        /// Return the ID of the variable.  The ID is either the
        /// internal_name of the variable if it has one, or its user
        /// facing name.
        const UString& id () const
        { return internal_name ().empty ()
                ? name ()
                : internal_name ();
        }

        IDebugger* debugger () const {return m_debugger;}
        void debugger (IDebugger *a_dbg) {m_debugger = a_dbg;}

        const UString& name () const {return m_name;}
        void name (const UString &a_name)
        {
            m_name_caption = a_name;
            m_name = a_name;
        }

        const UString& name_caption () const {return m_name_caption;}
        void name_caption (const UString &a_n) {m_name_caption = a_n;}

        const UString& value () const {return m_value;}
        void value (const UString &a_value) {m_value = a_value;}

        const UString& type () const {return m_type;}
        void type (const UString &a_type) {m_type = a_type;}
        void type (const string &a_type) {m_type = a_type;}

        const UString& visualizer () const {return m_visualizer;}
        void visualizer (const UString &a) {m_visualizer = a;}

        const UString& display_hint () const {return m_display_hint;};
        void display_hint (const UString &a) {m_display_hint = a;}

        /// Return true if this instance of Variable has a parent variable,
        /// false otherwise.
        bool has_parent () const
        {
            return m_parent != 0;
        }

        /// If this variable has a backend counterpart (e.g GDB
        /// Backend variable object) then, there are cases where a
        /// variable sub-object can be returned by the backend without
        /// being linked to its ancestor tree.  Such a variable
        /// appears as being structurally root (it has no parent), but
        /// is morally a sub-variable.  A variable that is
        /// structurally non-root is also morally non-root.
        ///
        /// To know if a variable is morally root, this function
        /// detects if the fully qualified internal name of the
        /// variable has dot (".") in it (e.g, "variable.member").  If
        /// does not, then the function assumes the variable is
        /// morally root and returns true.
        bool is_morally_root () const
        {
            if (has_parent ())
                return false;
            if (internal_name ().empty ())
                return !has_parent ();
            return (internal_name ().find (".") == UString::npos);
        }

        /// A getter of the parent Variable of the current instance.
        const VariableSafePtr parent () const
        {
            VariableSafePtr parent (m_parent, true/*add a reference*/);
            return parent;
        }

        /// Parent variable setter.
        /// We don't increase the ref count of the
        /// parent variable to avoid memory leaks due to refcounting cyles.
        /// The parent already holds a reference on this instance of
        /// Variable so we really should not hold a reference on it as
        /// well.
        void parent (Variable *a_parent)
        {
            m_parent = a_parent;
        }

        /// \return the root parent of this variable.
        const VariableSafePtr root () const
        {
            if (!has_parent ()) {
                return VariableSafePtr (this, true /*increase refcount*/);
            }
            return parent ()->root ();
        }

        void to_string (UString &a_str,
                        bool a_show_var_name = false,
                        const UString &a_indent_str="") const
        {
            if (a_show_var_name) {
                if (name () != "") {
                    a_str += a_indent_str + name ();
                }

                if (!internal_name ().empty ()) {
                    a_str += "(" + internal_name () + ")";
                }
            }
            if (value () != "") {
                if (a_show_var_name) {
                    a_str += "=";
                }
                a_str += value ();
            }
            if (members ().empty ()) {
                return;
            }
            UString indent_str = a_indent_str + "  ";
            a_str += "\n" + a_indent_str + "{";
            VariableList::const_iterator it;
            for (it = members ().begin (); it != members ().end (); ++it) {
                if (!(*it)) {continue;}
                a_str += "\n";
                (*it)->to_string (a_str, true, indent_str);
            }
            a_str += "\n" + a_indent_str + "}";
            a_str.chomp ();
        }

        void build_qname (UString &a_qname) const
        {
            UString qname;
            if (!parent ()) {
                a_qname = name ();
                if (!a_qname.raw ().empty () && a_qname.raw ()[0] == '*') {
                    a_qname.erase (0, 1);
                }
            } else if (parent ()) {
                parent ()->build_qname (qname);
                qname.chomp ();
                if (parent () && parent ()->name ()[0] == '*') {
                    qname += "->" + name ();
                } else {
                    qname += "." + name ();
                }
                a_qname = qname;
            } else {
                THROW ("should not be reached");
            }
        }

        void build_qualified_internal_name (UString &a_qname) const
        {
            UString qname;
            if (!parent ()) {
                a_qname = internal_name ();
            } else if (parent ()) {
                parent ()->build_qname (qname);
                qname.chomp ();
                qname += "." + name ();
                a_qname = qname;
            } else {
                THROW ("should not be reached");
            }
        }

        void set_dereferenced (VariableSafePtr a_derefed)
        {
            m_dereferenced  = a_derefed;
        }

        VariableSafePtr get_dereferenced ()
        {
            return m_dereferenced;
        }

        bool is_dereferenced ()
        {
            if (m_dereferenced) {return true;}
            return false;
        }

        bool is_copyable (const Variable &a_other) const
        {
            if (a_other.type () != type ()) {
                //can't copy a variable of different type
                return false;
            }
            //both variables must have same members
            if (members ().size () != a_other.members ().size ()) {
                return false;
            }

            VariableList::const_iterator it1, it2;
            //first make sure our members have the same types as their members
            for (it1=members ().begin (), it2=a_other.members ().begin ();
                 it1 != members ().end ();
                 it1++, it2++) {
                if (!*it1 || !*it2)
                    return false;
                if (!(*it1)->is_copyable (**it2))
                    return false;
            }
            return true;
        }

        bool copy (const Variable &a_other)
        {
            if (!is_copyable (a_other))
                return false;
            m_name = a_other.m_name;
            m_name_caption = a_other.m_name_caption;
            m_value = a_other.m_value;
            VariableList::iterator it1;
            VariableList::const_iterator it2;
            for (it1=m_members.begin (), it2=a_other.m_members.begin ();
                 it1 != m_members.end ();
                 it1++, it2++) {
                (*it1)->copy (**it2);
            }
            return true;
        }

        void set (const Variable &a_other)
        {
            m_name = a_other.m_name;
            m_value = a_other.m_value;
            m_type = a_other.m_type;
            VariableList::const_iterator it;
            m_members.clear ();
            for (it = a_other.m_members.begin ();
                 it != a_other.m_members.end ();
                 ++it) {
                VariableSafePtr var;
                var.reset (new Variable ());
                var->set (**it);
                append (var);
            }
        }

        unsigned int num_expected_children () const
        {
            return m_num_expected_children;
        }

        void num_expected_children (unsigned int a_in)
        {
            m_num_expected_children = a_in;
        }

        bool expects_children () const
        {
            return ((m_num_expected_children != 0)
                    || (has_more_children ()));
        }

        /// \return true if the current variable needs to be unfolded
        /// by a call to IDebugger::unfold_variable()
        bool needs_unfolding () const
        {
            return (expects_children () && members ().empty ());
        }

        /// Return the descendant of the current instance of Variable.
        /// \param a_internal_path the internal fully qualified path of the
        ///        descendant variable.
        const VariableSafePtr get_descendant
                                (const UString &a_internal_path) const
        {
            VariableSafePtr result;
            if (internal_name () == a_internal_path) {
                result.reset (this, true /*take refcount*/);
                return result;
            }
            for (VariableList::const_iterator it = m_members.begin ();
                 it != m_members.end ();
                 ++it) {
                if (*it && (*it)->internal_name () == a_internal_path) {
                    return *it;
                }
                result = (*it)->get_descendant (a_internal_path);
                if (result) {
                    return result;
                }
            }
            return result;
        }

        const UString& path_expression () const
        {
            return m_path_expression;
        }
        void path_expression (const UString &a_expr)
        {
            m_path_expression = a_expr;
        }

        bool in_scope () const {return m_in_scope;}
        void in_scope (bool a) {m_in_scope = a;}

        Format format () const {return m_format;}
        void format (Format a_format) {m_format = a_format;}

        bool needs_revisualizing () const {return m_needs_revisualizing;}
        void needs_revisualizing (bool a) {m_needs_revisualizing = a;}

        bool is_dynamic () const {return m_is_dynamic;}
        void is_dynamic (bool a) {m_is_dynamic = a;}

        bool has_more_children () const {return m_has_more_children;}
        void has_more_children (bool a) {m_has_more_children = a;}

    };//end class Variable

    enum State {
        // The inferior hasn't been loaded.
        NOT_STARTED=0,
        // The inferior has been loaded, but hasn't been run yet.
        INFERIOR_LOADED,
        // The inferior has started its execution, but is currently
        // stopped.
        READY,
        // The inferior is currently busy running.
        RUNNING,
        // The inferior has exited.
        PROGRAM_EXITED
    };//enum State

    static UString state_to_string (State a_state)
    {
        UString str;
        switch (a_state) {
        case NOT_STARTED:
            str = "NOT_STARTED";
            break;
        case INFERIOR_LOADED:
            str = "INFERIOR_LOADED";
            break;
        case READY:
            str = "READY";
            break;
        case RUNNING:
            str = "RUNNING";
            break;
        case PROGRAM_EXITED:
            str = "PROGRAM_EXITED";
            break;
        }
        return str;
    }

    enum StopReason {
        UNDEFINED_REASON=0,
        BREAKPOINT_HIT,
        WATCHPOINT_TRIGGER,
        READ_WATCHPOINT_TRIGGER,
        ACCESS_WATCHPOINT_TRIGGER,
        FUNCTION_FINISHED,
        LOCATION_REACHED,
        WATCHPOINT_SCOPE,
        END_STEPPING_RANGE,
        EXITED_SIGNALLED,
        EXITED,
        EXITED_NORMALLY,
        SIGNAL_RECEIVED
    };//end enum StopReason

    /// Return true if a StopReason represents a reason for exiting.
    static bool is_exited (enum StopReason a_reason)
    {
        if (a_reason == EXITED_SIGNALLED
            || a_reason == EXITED
            || a_reason == EXITED_NORMALLY)
            return true;
        return false;
    }

    typedef sigc::slot<void,
                       const std::map<string, IDebugger::Breakpoint>&>
        BreakpointsSlot;

    typedef sigc::slot<void, Loc&> LocSlot;

    virtual ~IDebugger () {}

    /// \name events you can connect to.

    /// @{

    virtual sigc::signal<void>& engine_died_signal () const = 0;

    virtual sigc::signal<void>& program_finished_signal () const = 0;

    virtual sigc::signal<void, const UString&>&
                                     console_message_signal () const = 0;

    virtual sigc::signal<void, const UString&>&
                                 target_output_message_signal () const = 0;

    virtual sigc::signal<void, const UString&>& log_message_signal () const=0;

    virtual sigc::signal<void,
                         const UString&/*command name*/,
                         const UString&/*command cookie*/>&
                                             command_done_signal () const=0;

    virtual sigc::signal<void>& connected_to_server_signal () const=0;

    virtual sigc::signal<void>& detached_from_target_signal () const=0;

    /// Signal emitted whenever the inferior is re-run, using the
    /// function IDebugger::re_run.
    virtual sigc::signal<void>& inferior_re_run_signal () const = 0;

    virtual sigc::signal<void,
                        const IDebugger::Breakpoint&,
                        const string& /*breakpoint number*/,
                        const UString & /*cookie*/>&
                                     breakpoint_deleted_signal () const=0;

    /// returns a list of breakpoints set. It is not all the breakpoints
    /// set. Some of the breakpoints in the list can have been
    /// already returned in a previous call. This is done so because
    /// IDebugger does not cache the list of breakpoints. This must
    /// be fixed at some point.
    virtual sigc::signal<void,
                         const map<string, IDebugger::Breakpoint>&,
                         const UString& /*cookie*/>&
                                         breakpoints_list_signal () const=0;

    virtual sigc::signal<void,
                        const std::map<string, IDebugger::Breakpoint>&,
                        const UString& /*cookie*/>&
                        breakpoints_set_signal () const = 0;

    virtual sigc::signal<void,
                         const vector<OverloadsChoiceEntry>&,
                         const UString& /*cookie*/>&
                                        got_overloads_choice_signal () const=0;

    virtual sigc::signal<void,
                         IDebugger::StopReason /*reason*/,
                         bool /*has frame*/,
                         const IDebugger::Frame&/*the frame*/,
                         int /*thread id*/,
                         const string& /*breakpoint number,
                                         meaningfull only when
                                         reason == IDebugger::BREAKPOINT_HIT*/,
                         const UString& /*cookie*/>& stopped_signal () const=0;

    virtual sigc::signal<void,
                         const list<int>/*thread ids*/,
                         const UString& /*cookie*/>&
                                        threads_listed_signal () const =0;

    virtual sigc::signal<void,
                         int/*thread id*/,
                         const IDebugger::Frame *const/*frame in thread*/,
                         const UString& /*cookie*/> &
                                             thread_selected_signal () const=0;

    virtual sigc::signal<void,
                        const vector<IDebugger::Frame>&,
                        const UString&>& frames_listed_signal () const=0;

    virtual sigc::signal<void,
                         //a frame number/argument list map
                         const map<int, list<IDebugger::VariableSafePtr> >&,
                         const UString& /*cookie*/>&
                                    frames_arguments_listed_signal () const=0;

    /// called when a core file is loaded.
    /// it signals the current frame, i.e the frame in which
    /// the core got dumped.
    virtual sigc::signal<void,
                        const IDebugger::Frame&,
                        const UString& /*cookie*/>&
                                            current_frame_signal () const = 0;


    virtual sigc::signal<void, const VariableList&, const UString& >&
                            local_variables_listed_signal () const = 0;

    virtual sigc::signal<void, const VariableList&, const UString& >&
                            global_variables_listed_signal () const = 0;

    /// Emitted as the result of the IDebugger::print_variable_value() call.
    virtual sigc::signal<void,
                         const UString&/*variable name*/,
                         const VariableSafePtr /*variable*/,
                         const UString& /*cookie*/>&
                                             variable_value_signal () const = 0;

    /// Emitted as the result of the IDebugger::get_variable_value() call.
    virtual sigc::signal<void,
                         const VariableSafePtr /*variable*/,
                         const UString& /*cookie*/>&
                                     variable_value_set_signal () const = 0;

    virtual sigc::signal<void,
                         const UString&/*variable name*/,
                         const VariableSafePtr /*variable*/,
                         const UString& /*cookie*/>&
                                    pointed_variable_value_signal () const = 0;

    /// Emitted as the result of the IDebugger::print_variable_type() call.
    virtual sigc::signal<void,
                         const UString&/*variable name*/,
                         const UString&/*type*/,
                         const UString&/*cookie*/>&
                                        variable_type_signal () const = 0;
    virtual sigc::signal<void,
                         const VariableSafePtr /*variable*/,
                         const UString&/*cookie*/>&
                                    variable_type_set_signal () const=0;

    /// Emitted as a the result of the IDebugger::dereference_variable is call.
    virtual sigc::signal<void,
                         const VariableSafePtr/*the variable we derefed*/,
                         const UString&/*cookie*/>&
                                      variable_dereferenced_signal () const=0;

    sigc::signal<void, const VariableSafePtr, const UString&>&
      variable_visualized_signal () const;

    virtual sigc::signal<void, const vector<UString>&, const UString&>&
                            files_listed_signal () const = 0;

    virtual sigc::signal<void,
                         int/*pid*/,
                         const UString&/*target path*/>&
                                            got_target_info_signal () const = 0;

    virtual sigc::signal<void>& running_signal () const=0;

    virtual sigc::signal<void,
                         const UString&/*signal name*/,
                         const UString&/*signal description*/>&
                                            signal_received_signal () const = 0;

    virtual sigc::signal<void, const UString&/*error message*/>&
                                                    error_signal () const = 0;

    virtual sigc::signal<void, IDebugger::State>&
                                        state_changed_signal () const = 0;

    virtual sigc::signal<void,
                         const std::map<register_id_t, UString>&,
                         const UString& >&
                                 register_names_listed_signal () const = 0;

    virtual sigc::signal<void,
                         const std::map<register_id_t, UString>&,
                         const UString& >&
                                   register_values_listed_signal () const = 0;

    virtual sigc::signal<void,
                         const UString&/*register name*/,
                         const UString&/*register value*/,
                         const UString&/*cookie*/>&
                                  register_value_changed_signal () const = 0;

    virtual sigc::signal<void,
                         const std::list<register_id_t>&,
                         const UString& >&
                                   changed_registers_listed_signal () const = 0;

    virtual sigc::signal <void,
                          size_t,/*start address*/
                          const std::vector<uint8_t>&,/*values*/
                          const UString&>&/*cookie*/
                                 read_memory_signal () const = 0;
    virtual sigc::signal <void,
                          size_t,/*start address*/
                          const std::vector<uint8_t>&,/*values*/
                          const UString& >&
                                 set_memory_signal () const = 0;

    // TODO: export informations about what file is being disassembled,
    // what function, which line (if possible) etc.
    // So that the code receiving the signal can adjust accordingly
    virtual sigc::signal<void,
                         const DisassembleInfo&,
                         const std::list<Asm>&,
                         const UString& /*cookie*/>&
                             instructions_disassembled_signal () const = 0;

    virtual sigc::signal<void, const VariableSafePtr, const UString&>&
                                 variable_created_signal () const = 0;

    /// This signal is emitted after IDebugger::delete_variable is
    /// called and the underlying backend-side variable object has
    /// been effectively deleted.  Note that when no instance of
    /// VariableSafePtr has been passed to the
    /// IDebugger::delete_variable method, the first argument of this
    /// signal slot is a null pointer to VariableSafePtr.
    virtual sigc::signal<void, const VariableSafePtr, const UString&>&
                                 variable_deleted_signal () const = 0;

    virtual sigc::signal<void, const VariableSafePtr, const UString&>&
                                 variable_unfolded_signal () const = 0;

    virtual sigc::signal<void, const VariableSafePtr, const UString&>&
                                 variable_expression_evaluated_signal () const = 0;

    /// This is a callback slot invoked upon completion of the
    /// IDebugger::list_changed_variables entry point.
    ///
    /// The parameters of the slots are the list of variables
    /// that changed, and the cookie passed to
    /// IDebugger::list_changed_variables.
    virtual sigc::signal<void, const VariableList&, const UString&>&
                                changed_variables_signal () const  = 0;

    virtual sigc::signal<void, VariableSafePtr, const UString&>&
                assigned_variable_signal () const = 0;
    /// @}

    virtual void do_init (IConfMgrSafePtr a_conf_mgr) = 0;

    virtual map<UString, UString>& properties () = 0;

    virtual void set_event_loop_context
                        (const Glib::RefPtr<Glib::MainContext> &) = 0;

    virtual void run_loop_iterations (int a_nb_iters) = 0;

    virtual bool busy () const = 0;

    virtual void set_non_persistent_debugger_path
                (const UString &a_full_path) = 0;

    virtual const UString& get_debugger_full_path () const  = 0;

    virtual void set_solib_prefix_path (const UString &a_name) = 0;

    virtual bool load_program (const UString &a_prog) = 0;

    virtual bool load_program (const UString &a_prog,
                               const vector<UString> &a_args) = 0;

    virtual bool load_program (const UString &a_prog,
                               const vector<UString> &a_args,
                               const UString &a_working_dir,
                               bool a_force = false) = 0;

    virtual bool load_program
                (const UString &a_prog,
                 const vector<UString> &a_argv,
                 const UString &working_dir,
                 const vector<UString> &a_source_search_dirs,
                 const UString &a_slave_tty_path,
                 int a_slave_tty_fd,
                 bool a_uses_launch_tty = false,
                 bool a_force = false) = 0;

    virtual void load_core_file (const UString &a_prog_file,
                                 const UString &a_core_file) = 0;

    virtual bool attach_to_target (unsigned int a_pid,
                                   const UString &a_tty_path="") = 0;

    virtual bool attach_to_remote_target (const UString &a_host,
                                          unsigned a_port) = 0;

    virtual bool attach_to_remote_target (const UString &a_serial_line) = 0;

	virtual bool attach_to_remote_target_test (const UString &a_linecommand) =0;

    virtual void detach_from_target (const UString &a_cookie="") = 0;

    virtual void disconnect_from_remote_target (const UString &a_cookie = "") = 0;

    virtual bool is_attached_to_target () const = 0;

    virtual void set_tty_path (const UString &a_tty_path) = 0;

    virtual void add_env_variables (const map<UString, UString> &a_vars) = 0;

    virtual map<UString, UString>& get_env_variables () = 0;

    virtual const UString& get_target_path () = 0;

    virtual void get_target_info (const UString &a_cookie="") = 0;

    virtual ILangTrait& get_language_trait () = 0;

    virtual bool is_variable_editable (const VariableSafePtr a_var) const = 0;

    virtual bool is_running () const = 0;

    virtual void do_continue (const UString &a_cookie="") = 0;

    virtual void run (const UString &a_cookie="") = 0;

    virtual void re_run (const DefaultSlot &) = 0;

    virtual IDebugger::State get_state () const = 0;

    virtual int get_current_frame_level () const = 0;

    virtual bool stop_target () = 0;

    virtual void exit_engine () = 0;

    virtual void step_over (const UString &a_cookie="") = 0;

    virtual void step_in (const UString &a_cookie="") = 0;

    virtual void step_out (const UString &a_cookie="") = 0;

    virtual void step_over_asm (const UString &a_cookie="") = 0;

    virtual void step_in_asm (const UString &a_cookie="") = 0;

    virtual void continue_to_position (const UString &a_path,
                                       gint a_line_num,
                                       const UString &a_cookie="") = 0;

    virtual void jump_to_position (const Loc &a_loc,
                                   const DefaultSlot &a_slot) = 0;

    virtual void set_breakpoint (const common::Loc &a_loc,
                                 const UString &a_condition,
                                 gint a_ignore_count,
                                 const BreakpointsSlot &a_slot,
                                 const UString &a_cookie = "") = 0;

    virtual void set_breakpoint (const UString &a_path,
                                 gint a_line_num,
                                 const UString &a_condition= "",
                                 gint a_ignore_count = 0,
                                 const UString &a_cookie = "") = 0;

    virtual void set_breakpoint (const UString &a_func_name,
                                 const UString &a_condition = "",
                                 gint a_ignore_count = 0,
                                 const UString &a_cookie = "") = 0;

        virtual void set_breakpoint (const UString &a_func_name,
                                     const BreakpointsSlot &a_slot,
                                     const UString &a_condition = "",
                                     gint a_ignore_count = 0,
                                     const UString &a_cookie = "") = 0;

    virtual void set_breakpoint (const Address &a_address,
                                 const UString &a_condition = "",
                                 gint a_ignore_count = 0,
                                 const UString &a_cookie = "") = 0;

    virtual void enable_breakpoint (const string& a_break_num,
                                    const BreakpointsSlot &a_slot,
                                    const UString &a_cookie="") = 0;

    virtual void enable_breakpoint (const string& a_break_num,
                                    const UString &a_cookie="") = 0;

    virtual void disable_breakpoint (const string& a_break_num,
                                     const UString &a_cookie="") = 0;

    virtual void set_breakpoint_ignore_count
                                        (const string& a_break_num,
                                         gint a_ignore_count,
                                         const UString &a_cookie = "") = 0;

    virtual void set_breakpoint_condition (const string& a_break_num,
                                           const UString &a_condition,
                                           const UString &a_cookie = "") = 0;

    virtual void enable_countpoint (const string& a_break_num,
                                    bool a_flag,
                                    const UString &a_cookie ="") = 0;

    virtual bool is_countpoint (const string &a_break_num) const = 0;

    virtual bool is_countpoint (const Breakpoint &a_breakpoint) const = 0;

    virtual void delete_breakpoint (const UString &a_path,
                                    gint a_line_num,
                                    const UString &a_cookie="") = 0;

    virtual void set_watchpoint (const UString &a_expression,
                                 bool a_write = true,
                                 bool a_read = false,
                                 const UString &a_cookie = "") = 0;

    virtual void set_catch (const UString &a_event,
                            const UString &a_cookie="") = 0;

    virtual void list_breakpoints (const UString &a_cookie="") = 0;

    virtual const map<string, Breakpoint>& get_cached_breakpoints () = 0;

    virtual bool get_breakpoint_from_cache (const string &a_num,
                                            IDebugger::Breakpoint &a_bp)
        const = 0;

    virtual void choose_function_overload (int a_overload_number,
                                           const UString &a_cookie="") = 0;

    virtual void choose_function_overloads (const vector<int> &a_numbers,
                                            const UString &a_cookie="") = 0;

    virtual void delete_breakpoint (const string &a_break_num,
                                    const UString &a_cookie="") = 0;

    virtual void list_threads (const UString &a_cookie="") = 0;

    virtual void select_thread (unsigned int a_thread_id,
                                const UString &a_cookie="") = 0;

    virtual unsigned int get_current_thread () const = 0;

    virtual void select_frame (int a_frame_id,
                               const UString &a_cookie="") = 0;

    virtual void list_frames (int a_low_frame=-1,
                              int a_high_frame=-1,
                              const UString &a_cookie="") = 0;

    virtual void list_frames (int a_low_frame,
                              int a_high_frame,
                              const FrameVectorSlot &a_slot,
                              const UString &a_cookie) = 0;

    virtual void list_frames_arguments (int a_low_frame=-1,
                                        int a_high_frame=-1,
                                        const UString &a_cookie="") = 0;

    virtual void list_frames_arguments (int a_low_frame,
                                        int a_high_frame,
                                        const FrameArgsSlot &a_slot,
                                        const UString &a_cookie) = 0;

    virtual void list_local_variables (const ConstVariableListSlot &a_slot,
                                       const UString &a_cookie="") = 0;

    virtual void list_local_variables (const UString &a_cookie="") = 0;

    virtual void list_global_variables (const UString &a_cookie="") = 0;

    virtual void evaluate_expression (const UString &a_expr,
                                      const UString &a_cookie="") = 0;

    virtual void call_function (const UString &a_call_expression,
                                const UString &a_cookie="") = 0;

    virtual void print_variable_value (const UString &a_var_name,
                                       const UString &a_cookie="")  = 0;

    virtual void get_variable_value (const VariableSafePtr &a_var,
                                     const UString &a_cookie="")  = 0;

    virtual void print_pointed_variable_value (const UString &a_var_name,
                                               const UString &a_cookie="")=0;

    virtual void print_variable_type (const UString &a_var_name,
                                      const UString &a_cookie="") = 0;

    virtual void get_variable_type (const VariableSafePtr &a_var,
                                    const UString &a_cookie="") = 0;

    virtual bool dereference_variable (const VariableSafePtr &a_var,
                                       const UString &a_cookie="") = 0;

    virtual void revisualize_variable (const VariableSafePtr a_var,
                                       const ConstVariableSlot &a_slot) = 0;

    virtual void list_files (const UString &a_cookie="") = 0;

    virtual void list_register_names (const UString &a_cookie="") = 0;
    virtual void list_changed_registers (const UString &a_cookie="") = 0;
    virtual void list_register_values (const UString &a_cookie="") = 0;
    virtual void list_register_values (std::list<register_id_t> a_registers,
                                       const UString &a_cookie="") = 0;
    virtual void set_register_value (const UString& a_reg_name,
                                     const UString& a_value,
                                     const UString& a_cookie="") = 0;

    virtual void read_memory (size_t a_start_addr, size_t a_num_bytes,
            const UString& a_cookie="") = 0;
    virtual void set_memory (size_t a_addr,
            const std::vector<uint8_t>& a_bytes,
            const UString& a_cookie="") = 0;

    typedef sigc::slot<void,
                       const DisassembleInfo&,
                       const std::list<Asm>& > DisassSlot;

    virtual void disassemble (size_t a_start_addr,
                              bool a_start_addr_relative_to_pc,
                              size_t a_end_addr,
                              bool a_end_addr_relative_to_pc,
                              bool a_pure_asm = true,
                              const UString &a_cookie = "") = 0;

    virtual void disassemble (size_t a_start_addr,
                              bool a_start_addr_relative_to_pc,
                              size_t a_end_addr,
                              bool a_end_addr_relative_to_pc,
                              const DisassSlot &a_slot,
                              bool a_pure_asm = true,
                              const UString &a_cookie = "") = 0;

    virtual void disassemble_lines (const UString &a_file_name,
                                    int a_line_num,
                                    int a_nb_disassembled_lines,
                                    bool a_pure_asm = true,
                                    const UString &a_cookie = "") = 0;

    virtual void disassemble_lines (const UString &a_file_name,
                                    int a_line_num,
                                    int a_nb_disassembled_lines,
                                    const DisassSlot &a_slot,
                                    bool a_pure_asm = true,
                                    const UString &a_cookie = "") = 0;

    virtual void create_variable (const UString &a_name,
                                  const UString &a_cookie = "") = 0;

    virtual void create_variable (const UString &a_name,
                                  const ConstVariableSlot &a_slot,
                                  const UString &a_cookie = "") = 0;

    /// If a variable has a backend counterpart (e.g, a variable object
    /// when using the GDB backend), then this method deletes the
    /// backend.  You should not use this method because the life cycle
    /// of variables backend counter parts is automatically
    /// tied to the life cycle of instances of IDebugger::Variable,
    /// unless you know what you are doing.
    ///
    /// Note that when the backend counter part is deleted, the
    /// IDebugger::variable_deleted_signal is invoked with the a_var
    /// variable in argument.
    ///
    /// \param a_var the variable which backend counter to delete.
    ///
    /// \param a_cookie a string cookie passed to the
    /// IDebugger::variable_deleted_signal.
    virtual void delete_variable (const VariableSafePtr a_var,
                                  const UString &a_cookie = "") = 0;

    /// If a variable has a backend counterpart (e.g, a variable object
    /// when using the GDB backend), then this method deletes the
    /// backend.  You should not use this method because the life cycle
    /// of variables backend counter parts is automatically
    /// tied to the life cycle of instances of IDebugger::Variable,
    /// unless you know what you are doing.
    ///
    /// Note that when the backend counter part is deleted, the
    /// IDebugger::variable_deleted_signal is invoked with the a_var
    /// variable in argument.
    ///
    /// \param a_var the variable which backend counter to delete.
    ///
    /// \param a_slot a slot asynchronously called when the backend
    /// variable oject is deleted.
    ///
    /// \param a_cookie a string cookie passed to the
    /// IDebugger::variable_deleted_signal.
    virtual void delete_variable (const VariableSafePtr a_var,
                                  const ConstVariableSlot&,
                                  const UString &a_cookie = "") = 0;

    /// Deletes a backend variable object (e.g, for GDB, a so called
    /// variable object) named by a given string.
    /// You should not use this method because the life cycle
    /// of variables backend counter parts is automatically
    /// tied to the life cycle of instances of IDebugger::Variable,
    /// unless you know what you are doing.
    ///
    /// Note that when the backend counter part is deleted, the
    /// IDebugger::variable_deleted_signal is invoked, with null pointer
    /// to IDebugger::Variable.
    ///
    /// \param a_internal_name the name of the backend variable object
    /// we want to delete.
    ///
    /// \param a_slot a slot that is going to be called
    /// asynchronuously when the backend object is deleted.
    ///
    /// \param a_cookie
    virtual void delete_variable (const UString &a_internal_name,
                                  const DefaultSlot &a_slot,
                                  const UString &a_cookie = "") = 0;

    virtual void unfold_variable (VariableSafePtr a_var,
                                  const UString &a_cookie = "") = 0;
    virtual void unfold_variable
                (VariableSafePtr a_var,
                 const ConstVariableSlot&,
                 const UString &a_cookie = "") = 0;

    virtual void assign_variable (const VariableSafePtr a_var,
                                  const UString &a_expression,
                                  const UString &a_cookie = "") = 0;

    virtual void assign_variable
                    (const VariableSafePtr a_var,
                     const UString &a_expression,
                     const ConstVariableSlot &a_slot,
                     const UString &a_cookie="") = 0;

    virtual void evaluate_variable_expr (const VariableSafePtr a_var,
                                         const UString &a_cookie = "") = 0;
    virtual void evaluate_variable_expr
            (const VariableSafePtr a_var,
             const ConstVariableSlot &a_slot,
             const UString &a_cookie = "")= 0;

    /// List the sub-variables of a_root (including a_root)
    /// which value changed since the last time this function was
    /// invoked.
    ///
    /// \param a_root the variable to consider
    ///
    /// \param a_cookie the cookie to be passed to the callback
    //// slot IDebugger::changed_variables_signal
    virtual void list_changed_variables (VariableSafePtr a_root,
                                         const UString &a_cookie = "") = 0;

    /// List the sub-variables of a_root (including a_root) which value
    /// changed since the last time this function was called.
    ///
    /// \param a_root the variable to consider
    /// 
    /// \param a_slot the slot to be invoked upon completion of this
    /// function.  That slot is going to be passed the list of
    /// sub-variables that have changed.
    /// 
    /// \a_cookie the cookie to be passed to the callback function
    /// IDebugger::changed_variables_signal
    virtual void list_changed_variables
            (VariableSafePtr a_root,
             const ConstVariableListSlot &a_slot,
             const UString &a_cookie="") = 0;

    virtual void query_variable_path_expr (const VariableSafePtr a_var,
                                           const UString &a_cookie = "") = 0;

    virtual void query_variable_path_expr (const VariableSafePtr a_var,
                                           const ConstVariableSlot &a_slot,
                                           const UString &a_cookie = "") = 0;

    virtual void query_variable_format (const VariableSafePtr a_var,
                                        const ConstVariableSlot &a_slot,
                                        const UString &a_cookie = "") = 0;

    virtual void set_variable_format (const VariableSafePtr a_var,
                                      const Variable::Format a_format,
                                      const UString &a_cookie = "") = 0;

    virtual void enable_pretty_printing (bool a_flag = true) = 0;

};//end IDebugger

NEMIVER_END_NAMESPACE (nemiver)

#endif //__NMV_I_DEBUGGER_H__

