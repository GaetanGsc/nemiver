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
#include "config.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include "common/nmv-str-utils.h"
#include "common/nmv-asm-utils.h"
#include "nmv-gdbmi-parser.h"
#include "nmv-debugger-utils.h"

using nemiver::common::UString;

static const UString GDBMI_PARSING_DOMAIN = "gdbmi-parsing-domain";
static const UString GDBMI_OUTPUT_DOMAIN = "gdbmi-output-domain";

#define LOG_PARSING_ERROR(a_from) \
do { \
Glib::ustring str_01 (m_priv->input, (a_from), m_priv->end - (a_from));\
LOG_ERROR ("parsing failed for buf: >>>" \
             << m_priv->input << "<<<" \
             << " cur index was: " << (int)(a_from)); \
} while (0)

#define LOG_PARSING_ERROR_MSG(a_from, msg) \
do { \
Glib::ustring str_01 (m_priv->input, (a_from), m_priv->end - (a_from));\
LOG_ERROR ("parsing failed for buf: >>>" \
             << m_priv->input << "<<<" \
             << " cur index was: " << (int)(a_from) \
             << ", reason: " << msg); \
} while (0)

#define END_OF_INPUT(cur) m_priv->index_passed_end (cur)

#define CHECK_END(a_current)                                \
    if (END_OF_INPUT (a_current)) {                         \
        LOG_ERROR ("hit end index " << (int) m_priv->end);  \
        return false;                                       \
}

#define PARSING_ERROR_IF_END(cur)                           \
    if (END_OF_INPUT (cur)) {                               \
        LOG_PARSING_ERROR (cur);                            \
        return false;                                       \
    }

#define SKIP_WS(a_from) \
while (!m_priv->index_passed_end (a_from)  \
       && isspace (RAW_CHAR_AT (a_from))) {     \
    CHECK_END (a_from);++a_from; \
}

#define SKIP_BLANK(a_from) m_priv->skip_blank (a_from)

#define RAW_CHAR_AT(cur) m_priv->raw_char_at (cur)

#define UCHAR_AT(cur) m_priv->m_priv->input (cur)

#define RAW_INPUT m_priv->input.raw ()

using namespace std;
using namespace nemiver::common;

NEMIVER_BEGIN_NAMESPACE (nemiver)

// *******************************
// <Definitions of GDBMITuple>
// *******************************
// Okay, we put the definitions of the GDBMITuple here
// (instead of having them in nmv-gdbmi-parser.h alongside the other GDBMI
// types) because otherwise, it won't build on OpenBSD, as nemiver is being
// built with gcc 3.3.5 on that system for now.
// So please, do not change this unless you are *SURE* it won't break on
// OpenBSD at least.

const list<GDBMIResultSafePtr>&
GDBMITuple::content () const
{
    return m_content;
}

void
GDBMITuple::content (const list<GDBMIResultSafePtr> &a_in)
{
    m_content = a_in;
}

void
GDBMITuple::append (const GDBMIResultSafePtr &a_result)
{
    m_content.push_back (a_result);
}

void
GDBMITuple::clear ()
{
    m_content.clear ();
}

// *******************************
// </Definitions of GDBMITuple>
// *******************************

// prefixes of command output records.
static const char* PREFIX_DONE = "^done";
static const char* PREFIX_RUNNING = "^running";
static const char* PREFIX_EXIT = "^exit";
static const char* PREFIX_CONNECTED = "^connected";
static const char* PREFIX_ERROR = "^error";
static const char* PREFIX_BKPT = "bkpt={";
static const char* PREFIX_BREAKPOINT_TABLE = "BreakpointTable={";
static const char* PREFIX_BREAKPOINT_MODIFIED_ASYNC_OUTPUT = "=breakpoint-modified,";
static const char* PREFIX_THREAD_IDS = "thread-ids={";
static const char* PREFIX_NEW_THREAD_ID = "new-thread-id=\"";
static const char* PREFIX_FILES = "files=[";
static const char* PREFIX_STACK = "stack=[";
static const char* PREFIX_FRAME = "frame={";
static const char* PREFIX_DEPTH = "depth=\"";
static const char* PREFIX_STACK_ARGS = "stack-args=[";
static const char* PREFIX_LOCALS = "locals=[";
static const char* PREFIX_VALUE = "value=\"";
static const char* PREFIX_REGISTER_NAMES = "register-names=";
static const char* PREFIX_CHANGED_REGISTERS = "changed-registers=";
static const char* PREFIX_REGISTER_VALUES = "register-values=";
static const char* PREFIX_MEMORY_VALUES = "addr=";
static const char* PREFIX_RUNNING_ASYNC_OUTPUT = "*running,";
static const char* PREFIX_STOPPED_ASYNC_OUTPUT = "*stopped,";
static const char* PREFIX_THREAD_SELECTED_ASYNC_OUTPUT = "=thread-selected,";
static const char* PREFIX_NAME = "name=\"";
static const char* PREFIX_VARIABLE_DELETED = "ndeleted=\"";
static const char* NDELETED = "ndeleted";
static const char* PREFIX_NUMCHILD = "numchild=\"";
static const char* NUMCHILD = "numchild";
static const char* PREFIX_VARIABLES_CHANGED_LIST = "changelist=[";
static const char* CHANGELIST = "changelist";
static const char* PREFIX_PATH_EXPR = "path_expr=";
static const char* PATH_EXPR = "path_expr";
static const char* PREFIX_ASM_INSTRUCTIONS= "asm_insns=";
const char* PREFIX_VARIABLE_FORMAT = "format=";

static bool grok_var_changed_list_components (GDBMIValueSafePtr a_value,
                                              list<VarChangePtr> &a_var_changes);

static bool
is_string_start (gunichar a_c)
{
    if (!isalpha (a_c) && a_c != '_' && a_c != '<' && a_c != '>') {
        return false;
    }
    return true;
}

/// remove the trailing chars "\\n" at the end of a string
/// these chars are found at the end gdb stream records.
void
remove_stream_record_trailing_chars (UString &a_str)
{
    if (a_str.size () < 2) {return;}
    UString::size_type i = a_str.size () - 1;
    LOG_D ("stream record: '" << a_str << "' size=" << (int) a_str.size (),
           GDBMI_PARSING_DOMAIN);
    if (a_str[i] == 'n' && a_str[i-1] == '\\') {
        i = i-1;
        a_str.erase (i, 2);
        a_str.append (1, '\n');
    }
}

IDebugger::StopReason
str_to_stopped_reason (const UString &a_str)
{
    if (a_str == "breakpoint-hit") {
        return IDebugger::BREAKPOINT_HIT;
    } else if (a_str == "watchpoint-trigger") {
        return IDebugger::WATCHPOINT_TRIGGER;
    } else if (a_str == "read-watchpoint-trigger") {
        return IDebugger::READ_WATCHPOINT_TRIGGER;
    } else if (a_str == "function-finished") {
        return IDebugger::FUNCTION_FINISHED;
    } else if (a_str == "location-reached") {
        return IDebugger::LOCATION_REACHED;
    } else if (a_str == "watchpoint-scope") {
        return IDebugger::WATCHPOINT_SCOPE;
    } else if (a_str == "end-stepping-range") {
        return IDebugger::END_STEPPING_RANGE;
    } else if (a_str == "exited-signalled") {
        return IDebugger::EXITED_SIGNALLED;
    } else if (a_str == "exited") {
        return IDebugger::EXITED;
    } else if (a_str == "exited-normally") {
        return IDebugger::EXITED_NORMALLY;
    } else if (a_str == "signal-received") {
        return IDebugger::SIGNAL_RECEIVED;
    } else {
        return IDebugger::UNDEFINED_REASON;
    }
}

bool
gdbmi_tuple_to_string (GDBMITupleSafePtr a_result,
                       UString &a_string)
{

    if (!a_result)
        return false;

    list<GDBMIResultSafePtr>::const_iterator it = 
        a_result->content ().begin ();
    UString str;
    bool is_ok = true;
    a_string = "{";

    if (it == a_result->content ().end ())
        goto end;

    is_ok = gdbmi_result_to_string (*it, str);
    if (!is_ok)
        goto end;
    a_string += str;
    ++it;

    for (; is_ok && it != a_result->content ().end (); ++it) {
        is_ok = gdbmi_result_to_string (*it, str);
        if (is_ok)
            a_string += "," + str;
    }

end:
    a_string += "}";
    return is_ok;
}

bool
gdbmi_result_to_string (GDBMIResultSafePtr a_result,
                        UString &a_string)
{
    if (!a_result)
        return false;

    UString variable, value;
    variable = a_result->variable ();

    if (!gdbmi_value_to_string (a_result->value (), value))
        return false;

    a_string = variable + "=" + value;
    return true;
}

bool
gdbmi_list_to_string (GDBMIListSafePtr a_list,
                      UString &a_string)
{
    if (!a_list)
        return false;

    bool is_ok = true;
    UString str;
    a_string = "[";
    switch (a_list->content_type ()) {
        case GDBMIList::RESULT_TYPE: {
            list<GDBMIResultSafePtr> results;
            a_list->get_result_content (results);
            list<GDBMIResultSafePtr>::const_iterator result_it = 
                results.begin ();
            if (result_it == results.end ())
                break;
            if (!gdbmi_result_to_string (*result_it, str))
                break;
            a_string += str;
            ++result_it;
            for (; is_ok && result_it != results.end (); ++result_it) {
                is_ok = gdbmi_result_to_string (*result_it, str);
                if (is_ok)
                    a_string += "," + str;
            }
        }
            break;
        case GDBMIList::VALUE_TYPE: {
            list<GDBMIValueSafePtr> values;
            a_list->get_value_content (values);
            list<GDBMIValueSafePtr>::const_iterator value_it = values.begin ();
            if (value_it == values.end ())
                break;
            if (!gdbmi_value_to_string (*value_it, str))
                break;
            a_string += str;
            ++value_it;
            for (; is_ok && value_it != values.end (); ++value_it) {
                is_ok = gdbmi_value_to_string (*value_it, str);
                if (is_ok)
                    a_string += "," + str;
            }
        }
            break;
        case GDBMIList::UNDEFINED_TYPE:
            a_string += "<undefined-gdbmi-list-type>";
            break;
    }
    a_string += "]";
    return is_ok;
}

bool
gdbmi_value_to_string (GDBMIValueSafePtr a_value,
                       UString &a_string)
{
    if (!a_value)
        return false;

    bool result = true;
    switch (a_value->content_type ()) {
        case GDBMIValue::EMPTY_TYPE:
            a_string = "";
            break;
        case GDBMIValue::STRING_TYPE:
            a_string = a_value->get_string_content ();
            result = true;
            break;
        case GDBMIValue::LIST_TYPE:
            result = gdbmi_list_to_string (a_value->get_list_content (),
                                           a_string);
            break;
        case GDBMIValue::TUPLE_TYPE:
            result = gdbmi_tuple_to_string (a_value->get_tuple_content (),
                                            a_string);
            break;
    }
    return result;
}

ostream&
operator<< (ostream &a_out, const GDBMIResultSafePtr &a_result)
{
    if (!a_result) {
        a_out << "";
    } else {
        UString str;
        gdbmi_result_to_string (a_result, str);
        a_out << str;
    }
    return a_out;
}

ostream&
operator<< (ostream &a_out, const GDBMITupleSafePtr &a_tuple)
{
    if (!a_tuple) {
        a_out << "<tuple nilpointer/>";
        return a_out;
    } else {
        UString str;
        gdbmi_tuple_to_string (a_tuple, str);
        a_out << str;
    }
    return a_out;
}

ostream&
operator<< (ostream &a_out, const GDBMIListSafePtr a_list)
{
    if (!a_list) {
        a_out << "<list nilpointer/>";
    } else {
        UString str;
        gdbmi_list_to_string (a_list, str);
        a_out << str;
    }
    return a_out;
}

ostream&
operator<< (ostream &a_out, const GDBMIValueSafePtr &a_val)
{
    if (!a_val) {
        a_out << "<value nilpointer/>";
    } else {
        UString str;
        gdbmi_value_to_string (a_val, str);
        a_out << str;
    }
    return a_out;
}

std::ostream&
operator<< (std::ostream &a_out, const IDebugger::Variable &a_var)
{
    a_out << "<variable>"
          << "<name>"<< a_var.name () << "</name>"
          << "<type>"<< a_var.type () << "</type>"
          << "<members>";

    if (!a_var.members ().empty ()) {
        list<IDebugger::VariableSafePtr>::const_iterator it;
        for (it = a_var.members ().begin ();
             it != a_var.members ().end ();
             ++it) {
            a_out << *(*it);
        }
    }
    a_out << "</members></variable>";
    return a_out;
}

std::ostream&
operator<< (std::ostream &a_out, const IDebugger::VariableSafePtr &a_var)
{
    if (a_var)
        a_out << a_var;
    else
        a_out << "";
    return a_out;
}

std::ostream&
operator<< (std::ostream &a_out,
            const std::list<IDebugger::VariableSafePtr> &a_vars)
{
    a_out << "<variablelist length='" << a_vars.size () << "'>";
    std::list<IDebugger::VariableSafePtr>::const_iterator i = a_vars.begin ();
    for (; i != a_vars.end (); ++i) {
        a_out << **i;
    }
    a_out << "</variablelist>";
    return a_out;
}

std::ostream&
operator<< (std::ostream &a_out, const VarChangePtr &a_change)
{
    a_out << "<varchange>";

    if (a_change->variable ())
        a_out << a_change->variable ();
    else
        a_out << "";

    a_out << "<newnumchildren>"
          << a_change->new_num_children ()
          << "</newnumchildren>"
          << "<newchildren>"
          << a_change->new_children ()
          << "</newchildren>"
          << "</varchange>";
    return a_out;
}

void
dump_gdbmi (const GDBMIResultSafePtr &a_result)
{
    std::cout << a_result;
}

void
dump_gdbmi (const GDBMITupleSafePtr &a_tuple)
{
    std::cout << a_tuple;
}

void
dump_gdbmi (const GDBMIListSafePtr &a_list)
{
    std::cout << a_list;
}

void
dump_gdbmi (const GDBMIValueSafePtr &a_var)
{
    std::cout << a_var;
}

void
dump_gdbmi (const IDebugger::Variable &a_var)
{
    std::cout << a_var;
}

struct QuickUStringLess : public std::binary_function<const UString,
                                                      const UString,
                                                      bool>
{
    bool operator() (const UString &a_lhs,
                     const UString &a_rhs)
    {
        if (!a_lhs.c_str ()) {return true;}
        if (!a_rhs.c_str ()) {return false;}
        //this is false for non ascii characters
        //but is way faster than UString::compare().
        int res = strncmp (a_lhs.c_str (), a_rhs.c_str (), a_lhs.bytes ());
        if (res < 0) {return true;}
        return false;
    }
};//end struct QuickUstringLess

//******************************
//<Parser methods>
//******************************
struct GDBMIParser::Priv {
    UString input;
    UString::size_type end;
    Mode mode;
    list<UString> input_stack;

    Priv (Mode a_mode = GDBMIParser::STRICT_MODE):
        end (0),
        mode (a_mode)
    {
    }

    Priv (const UString &a_input, Mode a_mode) :
        end (0),
        mode (a_mode)

    {
        push_input (a_input);
    }

    UString::value_type raw_char_at (UString::size_type at) const
    {
        return input.raw ()[at];
    }

    bool index_passed_end (UString::size_type a_index)
    {
        return a_index >= end;
    }

    bool skip_blank (UString::size_type &i)
    {
        while (!index_passed_end (i)
               && isblank (raw_char_at (i)))
            ++i;
        return true;
    }

    void set_input (const UString &a_input)
    {
        input = a_input;
        end = a_input.bytes ();
    }

    void clear_input ()
    {
        input.clear ();
        end = 0;
    }

    void push_input (const UString &a_input)
    {
        input_stack.push_front (a_input);
        set_input (a_input);
    }

    void pop_input ()
    {
        clear_input ();
        input_stack.pop_front ();
        if (!input_stack.empty ()) {
            set_input (input_stack.front ());
        }
    }
};//end class GDBMIParser;

GDBMIParser::GDBMIParser (Mode a_mode)
{
    m_priv.reset (new Priv (a_mode));
}

GDBMIParser::GDBMIParser (const UString &a_input, Mode a_mode)
{
    m_priv.reset (new Priv (a_input, a_mode));
}

GDBMIParser::~GDBMIParser ()
{
}

void
GDBMIParser::push_input (const UString &a_input)
{
    m_priv->push_input (a_input);
}

void
GDBMIParser::pop_input ()
{
    m_priv->pop_input ();
}

const UString&
GDBMIParser::get_input () const
{
    return m_priv->input;
}

void
GDBMIParser::set_mode (Mode a_mode)
{
    m_priv->mode = a_mode;
}

GDBMIParser::Mode
GDBMIParser::get_mode () const
{
    return m_priv->mode;
}

bool
GDBMIParser::parse_string (UString::size_type a_from,
                           UString::size_type &a_to,
                           UString &a_string)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur=a_from;
    CHECK_END (cur);

    UString::value_type ch = RAW_CHAR_AT (cur);
    if (!is_string_start (ch)) {
        LOG_PARSING_ERROR_MSG (cur,
                                "string doesn't start with a string char");
        return false;
    }
    UString::size_type str_start (cur), str_end (0);
    ++cur;
    CHECK_END (cur);

    for (;;) {
        ch = RAW_CHAR_AT (cur);
        if (isalnum (ch)
            || ch == '_'
            || ch == '-'
            || ch == '>'
            || ch == '<') {
            ++cur;
            if (!m_priv->index_passed_end (cur)) {
                continue;
            }
        }
        str_end = cur - 1;
        break;
    }
    Glib::ustring str (RAW_INPUT.c_str () + str_start,
                       str_end - str_start + 1);
    a_string = str;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_octal_escape (UString::size_type a_from,
                                 UString::size_type &a_to,
                                 unsigned char &a_byte_value)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur=a_from;

    if (m_priv->index_passed_end (cur+3))
        return false;

    if (RAW_CHAR_AT (cur) != '\\'
        || !isdigit (RAW_CHAR_AT (cur+1))
        || !isdigit (RAW_CHAR_AT (cur+2))
        || !isdigit (RAW_CHAR_AT (cur+3))) {
        return false;
    }

    a_byte_value = (RAW_CHAR_AT (cur+1) - '0') * 64 +
                   (RAW_CHAR_AT (cur+2) - '0') * 8 +
                   (RAW_CHAR_AT (cur+3) - '0');

    a_to = cur + 4;
    return true;
}

bool
GDBMIParser::parse_octal_escape_sequence (UString::size_type a_from,
                                          UString::size_type &a_to,
                                          UString &a_result)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur=a_from;

    if (m_priv->index_passed_end (cur+3))
        return false;

    CHECK_END (cur);
    CHECK_END (cur+1);

    unsigned char b=0;
    string raw;
    while (RAW_CHAR_AT (cur) == '\\') {
        if (parse_octal_escape (cur, cur, b)) {
            raw += b;
        } else {
            break;
        }
    }
    if (raw.empty ()) return false;
    try {
        // a_result = raw;
        a_result = Glib::filename_to_utf8 (raw);
    } catch (...) {
        a_result.assign (raw.size (), '?');
    }
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_c_string_body (UString::size_type a_from,
                                  UString::size_type &a_to,
                                  UString &a_string)
{
    UString::size_type cur=a_from;
    CHECK_END (cur);

    UString::value_type ch = RAW_CHAR_AT (cur), prev_ch;
    if (ch == '"') {
        a_string = "";
        a_to = cur;
        return true;
    }

    if (!isascii (ch) && ch != '\\') {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    std::string result;
    if (ch != '\\') {
        result += ch;
        ++cur;
    } else {
        UString seq;
        if (!m_priv->index_passed_end (cur + 3)
            && isdigit (RAW_CHAR_AT (cur + 1))
            && isdigit (RAW_CHAR_AT (cur + 2))
            && isdigit (RAW_CHAR_AT (cur + 3))
            && parse_octal_escape_sequence (cur, cur, seq)) {
            result += seq;
        } else {
            result += ch;
            ++cur;
        }
    }
    CHECK_END (cur);

    for (;;) {
        prev_ch = ch;
        ch = RAW_CHAR_AT (cur);
        if (isascii (ch)) {
            if (ch == '"' && prev_ch != '\\') {
                break;
            } else if (ch == '"' && prev_ch == '\\') {
                // So '"' was escaped as '\"'.  Let's expand it into
                // '"'.
                result.erase (result.end () - 1);
                result += ch;
                ++cur;
            } else if (ch == '\\') {
                UString seq;
                if (!m_priv->index_passed_end (cur+3)
                    && isdigit (RAW_CHAR_AT (cur +1))
                    && isdigit (RAW_CHAR_AT (cur +2))
                    && isdigit (RAW_CHAR_AT (cur +3))
                    && parse_octal_escape_sequence (cur, cur, seq)) {
                    ch = seq[seq.size ()-1];
                    result += seq;
                } else {
                    result += ch;
                    ++cur;
                }
            } else {
                result += ch;
                ++cur;
            }
            CHECK_END (cur);
            continue;
        } else {
	  result += ch;
	  ++cur;
	  if (m_priv->index_passed_end (cur))
	    break;
	}
    }

    if (ch != '"') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    a_string = result;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_c_string (UString::size_type a_from,
                             UString::size_type &a_to,
                             UString &a_c_string)
{
    UString::size_type cur=a_from;
    CHECK_END (cur);

    if (RAW_CHAR_AT (cur) != '"') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;
    CHECK_END (cur);

    UString str;
    if (!parse_c_string_body (cur, cur, str)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (RAW_CHAR_AT (cur) != '"') {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    ++cur;
    a_c_string = str;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_embedded_c_string_body (UString::size_type a_from,
                                           UString::size_type &a_to,
                                           UString &a_string)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    UString::size_type cur=a_from;
    CHECK_END (cur);
    CHECK_END (cur+1);

    if (RAW_CHAR_AT (cur) != '\\' || RAW_CHAR_AT (cur+1) != '"') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += 2;
    CHECK_END (cur);
    UString escaped_str;
    escaped_str += '"';

    //first walk the string, and unescape everything we find escaped
    UString::value_type ch=0, prev_ch=0;
    bool escaping = false, found_end=false;
    for (; !m_priv->index_passed_end (cur); ++cur) {
        ch = RAW_CHAR_AT (cur);
        if (ch == '\\') {
            if (escaping) {
                prev_ch = ch;
                escaped_str += ch;
                escaping = false;
            } else {
                escaping = true;
            }
        } else if (ch == '"') {
            if (escaping) {
                if (prev_ch != '\\') {
                    //found the end of string
                    found_end = true;
                }
                prev_ch = ch;
                escaped_str += ch;
                escaping = false;
                if (found_end) {
                    break;
                }
            } else {
                LOG_PARSING_ERROR (cur);
                return false;
            }
        } else {
            escaped_str += ch;
            prev_ch = ch;
            escaping = false;
        }
    }
    if (!found_end) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    //TODO:debug this.
    a_string = escaped_str;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_embedded_c_string (UString::size_type a_from,
                                      UString::size_type &a_to,
                                      UString &a_string)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    UString::size_type cur=a_from;
    CHECK_END (cur);

    if (RAW_CHAR_AT (cur) != '\\' || RAW_CHAR_AT (cur+1) != '"') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (!parse_embedded_c_string_body (cur, cur, a_string)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    a_to = ++cur;
    return true;
}

/// parse a GDB/MI RESULT data structure.
/// A result basically has the form:
/// VARIABLE=VALUE, where VALUE is either a CONST, a LIST, or a TUPLE.
/// So that's what the MI spec says. In practise, GDB can send RESULT
/// constructs that have the form VARIABLE. No =VALUE part there. So we
/// ought to be able to parse that as well, in GDBMIParser::BROKEN_MODE
//  parsing mode. Let's call that variant of RESULT a "SINGULAR RESULT".
///
/// Beware value is more complicated than what it looks like :-)
/// Look at the GDB/MI spec for more.
bool
GDBMIParser::parse_gdbmi_result (UString::size_type a_from,
                                 UString::size_type &a_to,
                                 GDBMIResultSafePtr &a_value)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    UString variable;
    GDBMIValueSafePtr value;
    bool is_singular = false;

    if (get_mode () == BROKEN_MODE
        && RAW_CHAR_AT (cur) == '"') {
        if (!parse_c_string (cur, cur, variable)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
    } else if (!parse_string (cur, cur, variable)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (get_mode () == BROKEN_MODE
        && (m_priv->index_passed_end (cur)
            || RAW_CHAR_AT (cur) != '=')) {
        is_singular = true;
        goto end;
    }

    CHECK_END (cur);
    SKIP_BLANK (cur);
    if (RAW_CHAR_AT (cur) != '=') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    LOG_D ("got gdbmi variable: " << variable, GDBMI_PARSING_DOMAIN);
    ++cur;
    CHECK_END (cur);

    if (!parse_gdbmi_value (cur, cur, value)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    THROW_IF_FAIL (value);

end:
    GDBMIResultSafePtr result (new GDBMIResult (variable, value, is_singular));
    THROW_IF_FAIL (result);
    a_to = cur;
    a_value = result;
    return true;
}

bool
GDBMIParser::parse_gdbmi_value (UString::size_type a_from,
                                UString::size_type &a_to,
                                GDBMIValueSafePtr &a_value)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    GDBMIValueSafePtr value;
    if (RAW_CHAR_AT (cur) == '"') {
        UString const_string;
        if (parse_c_string (cur, cur, const_string)) {
            value = GDBMIValueSafePtr (new GDBMIValue (const_string));
            LOG_D ("got str gdbmi value: '"
                    << const_string
                    << "'",
                   GDBMI_PARSING_DOMAIN);
        }
    } else if (RAW_CHAR_AT (cur) == '{') {
        GDBMITupleSafePtr tuple;
        if (parse_gdbmi_tuple (cur, cur, tuple)) {
            if (!tuple) {
                value = GDBMIValueSafePtr (new GDBMIValue ());
            } else {
                value = GDBMIValueSafePtr (new GDBMIValue (tuple));
            }
        }
    } else if (RAW_CHAR_AT (cur) == '[') {
        GDBMIListSafePtr list;
        if (parse_gdbmi_list (cur, cur, list)) {
            THROW_IF_FAIL (list);
            value = GDBMIValueSafePtr (new GDBMIValue (list));
        }
    } else {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (!value) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    a_value = value;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_gdbmi_tuple (UString::size_type a_from,
                                UString::size_type &a_to,
                                GDBMITupleSafePtr &a_tuple)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_CHAR_AT (cur) != '{') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;
    CHECK_END (cur);

    if (RAW_CHAR_AT (cur) == '}') {
        ++cur;
        a_to = cur;
        return true;
    }

    GDBMITupleSafePtr tuple;
    GDBMIResultSafePtr result;

    for (;;) {
        if (parse_gdbmi_result (cur, cur, result)) {
            THROW_IF_FAIL (result);
            SKIP_BLANK (cur);
            CHECK_END (cur);
            if (!tuple) {
                tuple = GDBMITupleSafePtr (new GDBMITuple);
                THROW_IF_FAIL (tuple);
            }
            tuple->append (result);
            if (RAW_CHAR_AT (cur) == ',') {
                ++cur;
                CHECK_END (cur);
                SKIP_BLANK (cur);
                continue;
            }
            if (RAW_CHAR_AT (cur) == '}') {
                ++cur;
            }
        } else {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        LOG_D ("getting out at char '"
               << (char)RAW_CHAR_AT (cur)
               << "', at offset '"
               << (int)cur
               << "' for text >>>"
               << m_priv->input
               << "<<<",
               GDBMI_PARSING_DOMAIN);
        break;
    }

    SKIP_BLANK (cur);
    a_to = cur;
    a_tuple = tuple;
    return true;
}

bool
GDBMIParser::parse_gdbmi_list (UString::size_type a_from,
                               UString::size_type &a_to,
                               GDBMIListSafePtr &a_list)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    GDBMIListSafePtr return_list;
    if (RAW_CHAR_AT (cur) != '[') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    CHECK_END (cur + 1);
    if (RAW_CHAR_AT (cur + 1) == ']') {
        a_list = GDBMIListSafePtr (new GDBMIList);
        cur += 2;
        a_to = cur;
        return true;
    }

    ++cur;
    CHECK_END (cur);
    SKIP_BLANK (cur);

    GDBMIValueSafePtr value;
    GDBMIResultSafePtr result;
    if ((isalpha (RAW_CHAR_AT (cur)) || RAW_CHAR_AT (cur) == '_')
         && parse_gdbmi_result (cur, cur, result)) {
        CHECK_END (cur);
        THROW_IF_FAIL (result);
        return_list = GDBMIListSafePtr (new GDBMIList (result));
        for (;;) {
            if (RAW_CHAR_AT (cur) == ',') {
                ++cur;
                SKIP_BLANK (cur);
                CHECK_END (cur);
                result.reset ();
                if (parse_gdbmi_result (cur, cur, result)) {
                    THROW_IF_FAIL (result);
                    return_list->append (result);
                    continue;
                }
            }
            break;
        }
    } else if (parse_gdbmi_value (cur, cur, value)) {
        CHECK_END (cur);
        THROW_IF_FAIL (value);
        return_list = GDBMIListSafePtr (new GDBMIList (value));
        for (;;) {
            if (RAW_CHAR_AT (cur) == ',') {
                ++cur;
                SKIP_BLANK (cur);
                CHECK_END (cur);
                value.reset ();
                if (parse_gdbmi_value (cur, cur, value)) {
                    THROW_IF_FAIL (value);
                    return_list->append (value);
                    continue;
                }
            }
            break;
        }
    } else {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (RAW_CHAR_AT (cur) != ']') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;

    a_to = cur;
    a_list = return_list;
    return true;
}

bool
GDBMIParser::parse_gdbmi_string_result (UString::size_type a_from,
                                        UString::size_type &a_to,
                                        UString &a_variable,
                                        UString &a_value)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    GDBMIResultSafePtr result;
    if (!parse_gdbmi_result (cur, cur, result) || !result) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    // The value of the RESULT must be a STRING
    if (!result->value ()
        || result->value ()->content_type () != GDBMIValue::STRING_TYPE
        || result->value ()->get_string_content ().empty ()) {
        LOG_ERROR ("expected a STRING value for the GDBMI variable");
        return false;
    }

    a_variable = result->variable ();
    a_value = result->value ()->get_string_content ();
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_stream_record (UString::size_type a_from,
                                  UString::size_type &a_to,
                                  Output::StreamRecord &a_record)
{
    UString::size_type cur=a_from;

    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    UString console, target, log;
    if (RAW_CHAR_AT (cur) == '~') {
        //console stream output
        ++cur;
        if (m_priv->index_passed_end (cur)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        if (!parse_c_string (cur, cur, console)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        SKIP_WS (cur);
        if (!m_priv->index_passed_end (cur + 1)
            && RAW_CHAR_AT (cur) == '>'
            && isspace (RAW_CHAR_AT (cur+1))) {
            cur += 2;
        }
        SKIP_BLANK (cur);
    } else if (RAW_CHAR_AT (cur) == '@') {
        //target stream output
        ++cur;
        if (m_priv->index_passed_end (cur)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        if (!parse_c_string (cur, cur, target)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
    } else if (RAW_CHAR_AT (cur) == '&') {
        //log stream output
        ++cur;
        if (m_priv->index_passed_end (cur)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        if (!parse_c_string (cur, cur, log)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
    } else {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    for (; 
         !m_priv->index_passed_end (cur) && isspace (RAW_CHAR_AT (cur)); 
         ++cur) {
    }

    bool found (false);
    if (!console.empty ()) {
        found = true;
        remove_stream_record_trailing_chars (console);
        a_record.debugger_console (console);
    }
    if (!target.empty ()) {
        found = true;
        remove_stream_record_trailing_chars (target);
        a_record.target_output (target);
    }
    if (!log.empty ()) {
        found = true;
        remove_stream_record_trailing_chars (log);
        a_record.debugger_log (log);
    }

    if (!found) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_stopped_async_output (UString::size_type a_from,
                                         UString::size_type &a_to,
                                         bool &a_got_frame,
                                         IDebugger::Frame &a_frame,
                                         map<UString, UString> &a_attrs)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    UString::size_type cur=a_from;

    if (m_priv->index_passed_end (cur)) {return false;}

    if (RAW_INPUT.compare (cur, strlen (PREFIX_STOPPED_ASYNC_OUTPUT),
                           PREFIX_STOPPED_ASYNC_OUTPUT)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += 9;
    if (m_priv->index_passed_end (cur)) {return false;}

    map<UString, UString> attrs;
    UString name;
    GDBMIResultSafePtr result;
    bool got_frame (false);
    IDebugger::Frame frame;
    while (true) {
        if (!RAW_INPUT.compare (cur, strlen (PREFIX_FRAME),
                                PREFIX_FRAME)) {
            if (!parse_frame (cur, cur, frame)) {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            got_frame = true;
        } else {
            if (!parse_attribute (cur, cur, name, result)) {break;}
            if (result
                && result->value ()
                && result->value ()->content_type ()
                      == GDBMIValue::STRING_TYPE) {
                attrs[name] = result->value ()->get_string_content ();
                LOG_D ("got " << name << ":" << attrs[name],
                       GDBMI_PARSING_DOMAIN);
            }
            name.clear (); result.reset ();
        }

        if (m_priv->index_passed_end (cur)) {break;}
        if (RAW_CHAR_AT (cur) == ',') {++cur;}
        if (m_priv->index_passed_end (cur)) {break;}
    }

    for (;
         !m_priv->index_passed_end (cur)
         && RAW_CHAR_AT (cur) != '\n';
         ++cur)
    {
    }

    if (RAW_CHAR_AT (cur) != '\n') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;

    a_got_frame = got_frame;
    if (a_got_frame) {
        a_frame = frame;
    }
    a_to = cur;
    a_attrs = attrs;
    return true;
}

bool
GDBMIParser::parse_running_async_output (UString::size_type a_from,
                                         UString::size_type &a_to,
                                         int &a_thread_id)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    UString::size_type cur=a_from,
                       prefix_len = strlen (PREFIX_RUNNING_ASYNC_OUTPUT);

    if (m_priv->index_passed_end (cur)) {return false;}

    if (RAW_INPUT.compare (cur, prefix_len,
                           PREFIX_RUNNING_ASYNC_OUTPUT)) {
        LOG_PARSING_ERROR_MSG (cur, "was expecting : '*running,'");
        return false;
    }
    cur += 9;
    if (m_priv->index_passed_end (cur)) {return false;}

    UString name, value;
    if (!parse_attribute (cur, cur, name, value)) {
        LOG_PARSING_ERROR_MSG (cur, "was expecting an attribute");
        return false;
    }
    if (name != "thread-id") {
        LOG_PARSING_ERROR_MSG (cur, "was expecting attribute 'thread-id'");
        return false;
    }
    if (value == "all")
        a_thread_id = -1;
    else
        a_thread_id = atoi (value.c_str ());

    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_thread_selected_async_output (UString::size_type a_from,
                                                 UString::size_type &a_to,
                                                 int &a_thread_id)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    UString::size_type cur = a_from,
                       prefix_len =
                           strlen (PREFIX_THREAD_SELECTED_ASYNC_OUTPUT);

    if (m_priv->index_passed_end (cur)) {return false;}

    if (RAW_INPUT.compare (cur, prefix_len,
                           PREFIX_THREAD_SELECTED_ASYNC_OUTPUT)) {
        LOG_PARSING_ERROR_MSG (cur, "was expecting : '=thread-selected,'");
        return false;
    }
    cur += prefix_len;
    if (m_priv->index_passed_end (cur)) {return false;}

    UString name, value;
    if (!parse_attribute (cur, cur, name, value)) {
        LOG_PARSING_ERROR_MSG (cur, "was expecting an attribute");
        return false;
    }
    if (name != "id" && name != "thread-id") {
        LOG_PARSING_ERROR_MSG (cur, "was expecting attribute 'thread-id' "
                                "or 'id'");
        return false;
    }
    unsigned thread_id = atoi (value.c_str ());
    if (!thread_id) {
        LOG_PARSING_ERROR_MSG (cur, "was expecting a non null thread-id");
        return false;
    }

    a_thread_id = thread_id;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_attribute (UString::size_type a_from,
                              UString::size_type &a_to,
                              UString &a_name,
                              GDBMIResultSafePtr &a_value)
{
    UString::size_type cur = a_from;

    if (m_priv->index_passed_end (cur)
        || !is_string_start (RAW_CHAR_AT (cur))) {return false;}

    GDBMIResultSafePtr result;
    if (!parse_gdbmi_result (cur, a_to, result)
        || !result
        || result->variable ().empty ()
        || !result->value ()) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    a_name = result->variable ();
    a_value = result;
    return true;
}

bool
GDBMIParser::parse_attribute (UString::size_type a_from,
                              UString::size_type &a_to,
                              UString &a_name,
                              UString &a_value)
{

    GDBMIResultSafePtr result;
    bool is_ok = parse_attribute (a_from, a_to, a_name, result);
    if (!is_ok)
        return false;
    gdbmi_value_to_string (result->value (), a_value);
    return true;
}

bool
GDBMIParser::parse_attributes (UString::size_type a_from,
                               UString::size_type &a_to,
                               map<UString, UString> &a_attrs)
{
    UString::size_type cur = a_from;

    if (m_priv->index_passed_end (cur)) {return false;}

    UString name, value;
    map<UString, UString> attrs;

    while (true) {
        if (!parse_attribute (cur, cur, name, value)) {
            break;
        }
        if (!name.empty () && !value.empty ()) {
            attrs[name] = value;
            name.clear (); value.clear ();
        }

        while (isspace (RAW_CHAR_AT (cur))) {++cur;}
        if (m_priv->index_passed_end (cur) || RAW_CHAR_AT (cur) != ',') {
            break;
        }
        if (m_priv->index_passed_end (++cur)) {break;}
    }
    a_attrs = attrs;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_frame (UString::size_type a_from,
                          UString::size_type &a_to,
                          IDebugger::Frame &a_frame)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (m_priv->input.compare (a_from, strlen (PREFIX_FRAME), PREFIX_FRAME)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMIResultSafePtr result;
    if (!parse_gdbmi_result (cur, cur, result)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    THROW_IF_FAIL (result);

    if (result->variable () != "frame") {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (!result->value ()
        ||result->value ()->content_type ()
                != GDBMIValue::TUPLE_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMITupleSafePtr result_value_tuple =
                                result->value ()->get_tuple_content ();
    if (!result_value_tuple) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    list<GDBMIResultSafePtr>::const_iterator res_it;
    GDBMIResultSafePtr tmp_res;
    IDebugger::Frame frame;
    UString name, value;
    for (res_it = result_value_tuple->content ().begin ();
         res_it != result_value_tuple->content ().end ();
         ++res_it) {
        if (!(*res_it)) {continue;}
        tmp_res = *res_it;
        if (!tmp_res->value ()
            ||tmp_res->value ()->content_type () != GDBMIValue::STRING_TYPE) {
            continue;
        }
        name = tmp_res->variable ();
        value = tmp_res->value ()->get_string_content ();
        if (name == "level") {
            frame.level (atoi (value.c_str ()));
        } else if (name == "addr") {
            frame.address () = value.raw ();
        } else if (name == "func") {
            frame.function_name (value.raw ());
        } else if (name == "file") {
            frame.file_name (value);
        } else if (name == "fullname") {
            frame.file_full_name (value);
        } else if (name == "line") {
            frame.line (atoi (value.c_str ()));
        }
    }
    a_frame = frame;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_output_record (UString::size_type a_from,
                                  UString::size_type &a_to,
                                  Output &a_output)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
	LOG_DD ("trace10");
    UString::size_type cur = a_from;

    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
		LOG_DD ("trace11");
        return false;
    }

    Output output;
	int y = 0;
    while (RAW_CHAR_AT (cur) == '*'
           || RAW_CHAR_AT (cur) == '~'
           || RAW_CHAR_AT (cur) == '@'
           || RAW_CHAR_AT (cur) == '&'
           || RAW_CHAR_AT (cur) == '+'
           || RAW_CHAR_AT (cur) == '=') {
        Output::OutOfBandRecord oo_record;
        if (!parse_out_of_band_record (cur, cur, oo_record)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        output.has_out_of_band_record (true);
		LOG_DD ("trace12");
		y++;
		if (y>=100) {
	LOG_DD ("force out after 100");
		
			return false;
		}
        output.out_of_band_records ().push_back (oo_record);

	}

    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
		LOG_DD ("trace13");
        return false;
    }

    if (RAW_CHAR_AT (cur) == '^') {
        Output::ResultRecord result_record;
        if (parse_result_record (cur, cur, result_record)) {
            output.has_result_record (true);
            output.result_record (result_record);
        }
        if (m_priv->index_passed_end (cur)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
    }

    while (!m_priv->index_passed_end (cur)
           && isspace (RAW_CHAR_AT (cur))) {++cur;}

    if (!RAW_INPUT.compare (cur, 5, "(gdb)")) {
        cur += 5;
		LOG_DD ("trace14");
    }

    if (cur == a_from) {
        //we didn't parse anything
        LOG_PARSING_ERROR (cur);
        return false;
    }

    a_output = output;
    a_to = cur;
    return true;
}

/// Skip everything from the current output report until the next
/// "(gdb)" marker, meaning the start of the next output record.
/// \param a_from the index where to start skipping from
/// \param a_to output parameter. If the function returns true this
/// parameter is set to the index of the character that comes right
/// after the "(gdb)" mark. Otherwise, it's set right after the end of
/// the stream.
/// \return true if "(gdb)" was found, false if we reached end of
/// stream before.
bool
GDBMIParser::skip_output_record (UString::size_type a_from,
                                 UString::size_type &a_to)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    bool found = false;
    while (!found) {
        if (m_priv->index_passed_end (cur + 5)) {
            while (!m_priv->index_passed_end (cur))
                ++cur;
            break;
        }
        if (RAW_CHAR_AT (cur) == '('
            && RAW_CHAR_AT (cur + 1) == 'g'
            && RAW_CHAR_AT (cur + 2) == 'd'
            && RAW_CHAR_AT (cur + 3) == 'b'
            && RAW_CHAR_AT (cur + 4) == ')')
            found = true;
        cur += 5;
    }
    a_to = cur;
    return found;
}

bool
GDBMIParser::parse_out_of_band_record (UString::size_type a_from,
                                       UString::size_type &a_to,
                                       Output::OutOfBandRecord &a_record)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    UString::size_type cur=a_from;
    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    Output::OutOfBandRecord record;
    if (RAW_CHAR_AT (cur) == '~' ||
        RAW_CHAR_AT (cur) == '@' ||
        RAW_CHAR_AT (cur) == '&') {
        Output::StreamRecord stream_record;
        if (!parse_stream_record (cur, cur, stream_record)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        record.has_stream_record (true);
        record.stream_record (stream_record);

        while (m_priv->index_passed_end (cur)
               && isspace (RAW_CHAR_AT (cur))) {++cur;}
    }

    if (!RAW_INPUT.compare (cur, strlen (PREFIX_STOPPED_ASYNC_OUTPUT),
                            PREFIX_STOPPED_ASYNC_OUTPUT)) {
        map<UString, UString> attrs;
        bool got_frame (false);
        IDebugger::Frame frame;
        if (!parse_stopped_async_output (cur, cur, got_frame, frame, attrs)) {
            LOG_PARSING_ERROR_MSG (cur,
                                    "could not parse the expected "
                                    "stopped async output");
            return false;
        }
        record.is_stopped (true);
        record.stop_reason (str_to_stopped_reason (attrs["reason"]));
        if (got_frame) {
            record.frame (frame);
            record.has_frame (true);
        }

        if (attrs.find ("bkptno") != attrs.end ()) {
            record.breakpoint_number (atoi (attrs["bkptno"].c_str ()));
        }
        if (attrs.find ("wpnum") != attrs.end ()) {
            record.breakpoint_number (atoi (attrs["wpnum"].c_str ()));
            LOG_D ("wpnum:" << attrs["wpnum"], GDBMI_PARSING_DOMAIN);
        }
        record.thread_id (atoi (attrs["thread-id"].c_str ()));
        record.signal_type (attrs["signal-name"]);
        record.signal_meaning (attrs["signal-meaning"]);
        goto end;
    }

    if (!RAW_INPUT.compare (cur, strlen (PREFIX_RUNNING_ASYNC_OUTPUT),
                            PREFIX_RUNNING_ASYNC_OUTPUT)) {
        int thread_id;
        if (!parse_running_async_output (cur, cur, thread_id)) {
            LOG_PARSING_ERROR_MSG (cur,
                                    "could not parse the expected "
                                    "running async output");
            return false;
        }
        record.thread_id (thread_id);
        record.is_running (true);
        goto end;
    }

    if (!RAW_INPUT.compare (cur,
                            strlen (PREFIX_THREAD_SELECTED_ASYNC_OUTPUT),
                            PREFIX_THREAD_SELECTED_ASYNC_OUTPUT)) {
        int thread_id;
        if (!parse_thread_selected_async_output (cur, cur, thread_id)) {
            LOG_PARSING_ERROR_MSG (cur,
                                    "could not parse the expected "
                                    "running async output");
            return false;
        }
        record.thread_id (thread_id);
        record.thread_selected (true);
        goto end;
    }

    if (!RAW_INPUT.compare (cur,
                            strlen (PREFIX_BREAKPOINT_MODIFIED_ASYNC_OUTPUT),
                            PREFIX_BREAKPOINT_MODIFIED_ASYNC_OUTPUT)) {
        IDebugger::Breakpoint bp;
        if (!parse_breakpoint_modified_async_output (cur, cur, bp)) {
            LOG_PARSING_ERROR_MSG (cur,
                                   "could not parse the expected "
                                   "breakpoint modifed async output");
            return false;
        }
        record.modified_breakpoint (bp);
        goto end;
    }

    if (RAW_CHAR_AT (cur) == '=' || RAW_CHAR_AT (cur) == '*') {
       //this is an unknown async notification sent by gdb.
       //For now, the only one
       //I have seen like this is of the form:
       //'=thread-created,id=1',
       //and the notification ends with a '\n' character.
       //Of course it is not documented
       //Let's ignore this by now
       while (RAW_CHAR_AT (cur) != '\n') {++cur;}
       ++cur;//consume the '\n' character
    }

end:

    while (!m_priv->index_passed_end (cur)
           && isspace (RAW_CHAR_AT (cur))) {++cur;}
	    a_to = cur;
    a_record = record;
    if (a_from == cur) {
		LOG_DD("detect a_to == a_from");
		return false;
	}
	return true;
}

bool
GDBMIParser::parse_result_record (UString::size_type a_from,
                                  UString::size_type &a_to,
                                  Output::ResultRecord &a_record)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    UString::size_type cur=a_from;
    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    UString name, value;
    Output::ResultRecord result_record;
    if (!RAW_INPUT.compare (cur, strlen (PREFIX_DONE), PREFIX_DONE)) {
        cur += 5;
        result_record.kind (Output::ResultRecord::DONE);

        if (!m_priv->index_passed_end (cur) && RAW_CHAR_AT (cur) == ',') {

fetch_gdbmi_result:
            cur++;
            if (m_priv->index_passed_end (cur)) {
                LOG_PARSING_ERROR (cur);
                return false;
            }

            if (!RAW_INPUT.compare (cur, strlen (PREFIX_BKPT),
                                    PREFIX_BKPT)) {
                IDebugger::Breakpoint breakpoint;
                if (parse_breakpoint (cur, cur, breakpoint)) {
                    result_record.breakpoints ()[breakpoint.id ()] =
                    breakpoint;
                }
            } else if (!m_priv->input.compare (cur,
                                               strlen (PREFIX_BREAKPOINT_TABLE),
                                               PREFIX_BREAKPOINT_TABLE)) {
                map<string, IDebugger::Breakpoint> breaks;
                if (parse_breakpoint_table (cur, cur, breaks)) {
                    result_record.breakpoints () = breaks;
                }
            } else if (!m_priv->input.compare (cur, strlen (PREFIX_THREAD_IDS),
                        PREFIX_THREAD_IDS)) {
                std::list<int> thread_ids;
                if (parse_threads_list (cur, cur, thread_ids)) {
                    result_record.thread_list (thread_ids);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_NEW_THREAD_ID),
                                           PREFIX_NEW_THREAD_ID)) {
                IDebugger::Frame frame;
                int thread_id=0;
                if (parse_new_thread_id (cur, cur, thread_id, frame)) {
                    //finish this !
                    result_record.thread_id_selected_info (thread_id, frame);
                }
            } else if (!m_priv->input.compare (cur, strlen (PREFIX_FILES),
                        PREFIX_FILES)) {
                vector<UString> files;
                if (!parse_file_list (cur, cur, files)) {
                    LOG_PARSING_ERROR (cur);
                    return false;
                }
                result_record.file_list (files);
                LOG_D ("parsed a list of files: "
                       << (int) files.size (),
                       GDBMI_PARSING_DOMAIN);
            } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_STACK),
                                           PREFIX_STACK)) {
                vector<IDebugger::Frame> call_stack;
                if (!parse_call_stack (cur, cur, call_stack)) {
                    LOG_PARSING_ERROR (cur);
                    return false;
                }
                result_record.call_stack (call_stack);
                LOG_D ("parsed a call stack of depth: "
                       << (int) call_stack.size (),
                       GDBMI_PARSING_DOMAIN);
                vector<IDebugger::Frame>::iterator frame_iter;
                for (frame_iter = call_stack.begin ();
                     frame_iter != call_stack.end ();
                     ++frame_iter) {
                    LOG_D ("function-name: " << frame_iter->function_name (),
                           GDBMI_PARSING_DOMAIN);
                }
            } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_FRAME),
                                           PREFIX_FRAME)) {
                IDebugger::Frame frame;
                if (!parse_frame (cur, cur, frame)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed result", GDBMI_PARSING_DOMAIN);
                    result_record.current_frame_in_core_stack_trace (frame);
                }
            } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_DEPTH),
                                           PREFIX_DEPTH)) {
                GDBMIResultSafePtr result;
                parse_gdbmi_result (cur, cur, result);
                THROW_IF_FAIL (result);
                LOG_D ("parsed result", GDBMI_PARSING_DOMAIN);
            } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_STACK_ARGS),
                                           PREFIX_STACK_ARGS)) {
                map<int, list<IDebugger::VariableSafePtr> > frames_args;
                if (!parse_stack_arguments (cur, cur, frames_args)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed stack args", GDBMI_PARSING_DOMAIN);
                }
                result_record.frames_parameters (frames_args);
            } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_LOCALS),
                                           PREFIX_LOCALS)) {
                list<IDebugger::VariableSafePtr> vars;
                if (!parse_local_var_list (cur, cur, vars)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed local vars", GDBMI_PARSING_DOMAIN);
                    result_record.local_variables (vars);
                }
            } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_VALUE),
                                           PREFIX_VALUE)) {
                // FIXME: this case will parse any response from
                // -data-evaluate-expression, including the response from
                // setting the value of a register or any other expression
                // evaluation.  Currently all of these cases can be parsed as a
                // variable, but there's no guarantee that this is the case.
                // Perhaps this needs to be reworked somehow
                IDebugger::VariableSafePtr var;
                if (!parse_variable_value (cur, cur, var)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed var value", GDBMI_PARSING_DOMAIN);
                    THROW_IF_FAIL (var);
                    result_record.variable_value (var);
                }
            } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_REGISTER_NAMES),
                                           PREFIX_REGISTER_NAMES)) {
                std::map<IDebugger::register_id_t, UString> regs;
                if (!parse_register_names (cur, cur, regs)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed register names", GDBMI_PARSING_DOMAIN);
                    result_record.register_names (regs);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_CHANGED_REGISTERS),
                                           PREFIX_CHANGED_REGISTERS)) {
                std::list<IDebugger::register_id_t> regs;
                if (!parse_changed_registers (cur, cur, regs)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed changed register", GDBMI_PARSING_DOMAIN);
                    result_record.changed_registers (regs);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_REGISTER_VALUES),
                                           PREFIX_REGISTER_VALUES)) {
                std::map<IDebugger::register_id_t, UString>  values;
                if (!parse_register_values (cur, cur,  values)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed register values", GDBMI_PARSING_DOMAIN);
                    result_record.register_values (values);
                }
            } else if (!m_priv->input.compare (cur,
                                               strlen (PREFIX_MEMORY_VALUES),
                                               PREFIX_MEMORY_VALUES)) {
                size_t addr;
                std::vector<uint8_t>  values;
                if (!parse_memory_values (cur, cur, addr, values)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed memory values", GDBMI_PARSING_DOMAIN);
                    result_record.memory_values (addr, values);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_ASM_INSTRUCTIONS),
                                           PREFIX_ASM_INSTRUCTIONS)) {
                std::list<common::Asm> asm_instrs;
                if (!parse_asm_instruction_list (cur, cur,
                                                 asm_instrs)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed asm instruction list", GDBMI_PARSING_DOMAIN);
                    result_record.asm_instruction_list (asm_instrs);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_NAME),  
                                           PREFIX_NAME)) {
                IDebugger::VariableSafePtr var;
                if (!parse_variable (cur, cur, var)) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    THROW_IF_FAIL (var);
                    LOG_D ("parsed variable, name: "
                           << var->name ()
                           << "internal name: "
                           << var->internal_name (),
                           GDBMI_PARSING_DOMAIN);
                    result_record.variable (var);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_VARIABLE_DELETED),
                                           PREFIX_VARIABLE_DELETED)) {
                unsigned int nb_variables_deleted = 0;
                if (parse_variables_deleted (cur, cur, nb_variables_deleted)
                    && nb_variables_deleted) {
                    result_record.number_of_variables_deleted
                                                        (nb_variables_deleted);
                } else {
                    LOG_PARSING_ERROR (cur);
                }
            } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_NUMCHILD),
                                           PREFIX_NUMCHILD)) {
                vector<IDebugger::VariableSafePtr> vars;
                if (parse_var_list_children (cur, cur, vars)) {
                    result_record.variable_children (vars);
                } else {
                    LOG_PARSING_ERROR (cur);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_VARIABLES_CHANGED_LIST),
                                           PREFIX_VARIABLES_CHANGED_LIST)) {
                list<VarChangePtr> var_changes;
                if (parse_var_changed_list (cur, cur, var_changes)) {
                    result_record.var_changes (var_changes);
                } else {
                    LOG_PARSING_ERROR (cur);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_PATH_EXPR),
                                           PREFIX_PATH_EXPR)) {
                UString var_expr;
                if (parse_var_path_expression (cur, cur, var_expr)) {
                    result_record.path_expression (var_expr);
                } else {
                    LOG_PARSING_ERROR (cur);
                }
            } else if (!RAW_INPUT.compare (cur,
                                           strlen (PREFIX_VARIABLE_FORMAT),
                                           PREFIX_VARIABLE_FORMAT)) {
                IDebugger::Variable::Format format =
                            IDebugger::Variable::UNDEFINED_FORMAT;
                UString value;
                if (parse_variable_format (cur, cur, format, value)) {
                    result_record.variable_format (format);
                    if (!value.empty ()) {
                        IDebugger::VariableSafePtr var (new IDebugger::Variable);
                        var->value (value);
                        result_record.variable_value (var);
                    }
                } else {
                    LOG_PARSING_ERROR (cur);
                }
            } else {
                GDBMIResultSafePtr result;
                if (!parse_gdbmi_result (cur, cur, result)
                    || !result) {
                    LOG_PARSING_ERROR (cur);
                } else {
                    LOG_D ("parsed unknown gdbmi result",
                           GDBMI_PARSING_DOMAIN);
                }
            }

            if (RAW_CHAR_AT (cur) == ',') {
                goto fetch_gdbmi_result;
            }

            //skip the remaining things we couldn't parse, until the
            //'end of line' character.
            for (;
                 !m_priv->index_passed_end (cur) && RAW_CHAR_AT (cur) != '\n';
                 ++cur) {}
        }
    } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_RUNNING),
                                   PREFIX_RUNNING)) {
        result_record.kind (Output::ResultRecord::RUNNING);
        cur += 8;
        for (;
             !m_priv->index_passed_end (cur) && RAW_CHAR_AT (cur) != '\n';
             ++cur) {}
    } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_EXIT),
                                   PREFIX_EXIT)) {
        result_record.kind (Output::ResultRecord::EXIT);
        cur += 5;
        for (;
             !m_priv->index_passed_end (cur) && RAW_CHAR_AT (cur) != '\n';
             ++cur) {}
    } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_CONNECTED),
                                   PREFIX_CONNECTED)) {
        result_record.kind (Output::ResultRecord::CONNECTED);
        cur += 10;
        for (;
             !m_priv->index_passed_end (cur)
             && RAW_CHAR_AT (cur) != '\n';
             ++cur) {
        }
    } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_ERROR),
                                   PREFIX_ERROR)) {
        result_record.kind (Output::ResultRecord::ERROR);
        cur += 6;
        CHECK_END (cur);
        if (!m_priv->index_passed_end (cur) && RAW_CHAR_AT (cur) == ',') {
            ++cur;
        }
        CHECK_END (cur);
        if (!parse_attribute (cur, cur, name, value)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        if (name != "") {
            LOG_DD ("got error with attribute: '"
                    << name << "':'" << value << "'");
            result_record.attrs ()[name] = value;
        } else {
            LOG_ERROR ("weird, got error with no attribute. continuing.");
        }
        for (;
             !m_priv->index_passed_end (cur)
             && RAW_CHAR_AT (cur) != '\n';
             ++cur) {
        }
    } else {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (RAW_CHAR_AT (cur) != '\n' && !m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    a_record = result_record;
    a_to = cur;
    return true;
}

/// Parse a GDBMI expression that contains only one breakpoint.
///
/// The expression parsed has either the form:
///
/// bkpt={number="1",type="breakpoint",disp="keep",
///       enabled="y",addr="0x000100d0",func="main",file="hello.c",
///       fullname="/home/foo/hello.c",line="5",thread-groups=["i1"],
///       times="0"}
///
/// Or the form:
///
/// {number="1",type="breakpoint",disp="keep",
///  enabled="y",addr="0x000100d0",func="main",file="hello.c",
///  fullname="/home/foo/hello.c",line="5",thread-groups=["i1"],
///  times="0"}
///
///  \parm a_from the index from which to parse the MI string.
///
///  \param a_to where the index has been advanced to, upon completion
///  of this function.
///
///  \param is_sub_breakpoint if true, means we are parsing the second
///  form of breakpoint expression above.  Otherwise, means we are
///  parsing the first form.
bool
GDBMIParser::parse_breakpoint_with_one_loc (Glib::ustring::size_type a_from,
                                            Glib::ustring::size_type &a_to,
                                            bool is_sub_breakpoint,
                                            IDebugger::Breakpoint &a_bkpt)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    Glib::ustring::size_type cur = a_from;

    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (!is_sub_breakpoint) {
        if (RAW_INPUT.compare (cur, strlen (PREFIX_BKPT), PREFIX_BKPT)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        cur += 6;
    } else {
        // We must be on the starting '{'.
        if (RAW_CHAR_AT (cur) != '{') {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        CHECK_END (++cur);
    }

    map<UString, UString> attrs;
    bool is_ok = parse_attributes (cur, cur, attrs);
    if (!is_ok) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (RAW_CHAR_AT (cur) != '}') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;

    UString pending = attrs["pending"];
    if (pending.empty ())
        pending = attrs["original-location"];
    else
        a_bkpt.is_pending (true);
    if (!pending.empty ()) {
        LOG_D ("got pending breakpoint: '" << pending << "'",
               GDBMI_OUTPUT_DOMAIN);
        bool is_file_location = false;
        string filename, line_num;
        // pending contains either the name of the function the breakpoint
        // has been set on, or the filename:linenumber location of the
        // breakpoint.
        // So if the pending string has the form
        // "path_to_filename:linenumber", with linenumber being an actual
        // number and being at the end of the string, then we know it's not
        // a function name.
        // Bear in mind that ':' can also be part of the path name. So we
        // really need to detect if the last ':' is the separator betwen
        // path and line number.
        is_file_location =
            str_utils::extract_path_and_line_num_from_location (pending.raw (),
                                                                filename,
                                                                line_num);

        if (is_file_location) {
            LOG_D ("filepath: '" << filename << "'", GDBMI_OUTPUT_DOMAIN);
            LOG_D ("linenum: '" << line_num << "'", GDBMI_OUTPUT_DOMAIN);
            string path = Glib::locale_from_utf8 (filename);
            if (Glib::path_is_absolute (path)) {
                attrs["file"] = Glib::locale_to_utf8
                                        (Glib::path_get_basename (path));
                attrs["fullname"] = Glib::locale_to_utf8 (path);
            } else {
                attrs["file"] = Glib::locale_to_utf8 (path);;
            }
            attrs["line"] = line_num;
        } else {
            attrs["func"] = pending;
        }
    }

    bool ignore_count_present = false;
    map<UString, UString>::iterator iter, null_iter = attrs.end ();
    // we use to require that the "fullname" property be present as
    // well, but it seems that a lot debug info set got shipped without
    // that property. Client code should do what they can with the
    // file property only.
    if ((iter = attrs.find ("number"))                            == null_iter
        || (iter = attrs.find ("enabled"))                        == null_iter
        || (!is_sub_breakpoint && ((iter = attrs.find ("type"))   == null_iter))
        || (!is_sub_breakpoint && ((iter = attrs.find ("disp"))   == null_iter))
        || (!is_sub_breakpoint && ((iter = attrs.find ("times"))  == null_iter))

	   // Non regular breakpoints like those set to catch fork
	   // events can have an empty address when set.
	   // || (iter = attrs.find ("addr"))    == null_iter
       ) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (!is_sub_breakpoint)
        a_bkpt.number (atoi (attrs["number"].c_str ()));
    else {
        UString num = attrs["number"];
        vector<UString> parts = str_utils::split (num, ".");
        if (parts.size () > 1)
            num = parts[1];
        else if (parts.size ())
            num = parts[0];
        a_bkpt.number (atoi (num.c_str ()));
    }

    if (attrs["enabled"] == "y") {
        a_bkpt.enabled (true);
    } else {
        a_bkpt.enabled (false);
    }
    if (str_utils::string_is_hexa_number (attrs["addr"]))
        a_bkpt.address () = attrs["addr"];
    if (!attrs["func"].empty ()) {
        a_bkpt.function (attrs["func"]);
    }
    if (!attrs["what"].empty ()) {
        // catchpoints or watchpoints
        // don't have a 'func' field, but they have a 'what' field
        // that describes the expression that was being watched.
        // something like "Exception throw" for catchpoint, or "varname"
        // for a watchpoint.
        a_bkpt.expression (attrs["what"]);
    }
    a_bkpt.file_name (attrs["file"]); //may be nil
    a_bkpt.file_full_name (attrs["fullname"]); //may be nil
    a_bkpt.line (atoi (attrs["line"].c_str ())); //may be nil
    if (a_bkpt.file_full_name ().empty ()
        && a_bkpt.file_name ().empty ()
        && (iter = attrs.find ("original-location")) != null_iter) {
        UString location = iter->second;
        string file_path;
        string line_num_str;
        str_utils::extract_path_and_line_num_from_location (location,
                                                            file_path,
                                                            line_num_str);
        // Line number must be present otherwise, that means
        // what was encoded in the "original-location" RESULT wasn't
        // "filepath:line-number"
        unsigned line_num = atoi (line_num_str.c_str ());
        if (!file_path.empty () && line_num) {
            a_bkpt.file_full_name (file_path);
            a_bkpt.line (line_num);
        }
    }
    if ((iter = attrs.find ("cond")) != null_iter) {
        a_bkpt.condition (iter->second);
    }
    a_bkpt.nb_times_hit (atoi (attrs["times"].c_str ()));
    if ((iter = attrs.find ("ignore")) != null_iter) {
        ignore_count_present = true;
        a_bkpt.ignore_count (atoi (iter->second.c_str ()));
    }

    string type = attrs["type"];
    if (type.find ("breakpoint") != type.npos)
        a_bkpt.type (IDebugger::Breakpoint::STANDARD_BREAKPOINT_TYPE);
    else if (type.find ("watchpoint") != type.npos)
        a_bkpt.type (IDebugger::Breakpoint::WATCHPOINT_TYPE);

    // Set the initial ignore count
    if (ignore_count_present)
        a_bkpt.initial_ignore_count (a_bkpt.ignore_count ()
                                     + a_bkpt.nb_times_hit ());

    //TODO: get the 'at' attribute that is present on targets that
    //are not compiled with -g.
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_breakpoint (Glib::ustring::size_type a_from,
                               Glib::ustring::size_type &a_to,
                               IDebugger::Breakpoint &a_bkpt)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    Glib::ustring::size_type cur = a_from;

    if (!parse_breakpoint_with_one_loc (cur, cur,
                                        /*is_sub_breakpoint=*/false,
                                        a_bkpt)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    while (true) {
        Glib::ustring::size_type saved_cur = cur;
        SKIP_BLANK (cur);

        if (!END_OF_INPUT (cur)) {
            if (RAW_CHAR_AT (cur) != ','
                || (++cur
                    && SKIP_BLANK (cur)
                    && !END_OF_INPUT (cur)
                    && RAW_CHAR_AT (cur) != '{')) {
                ;// Get out.
            } else {
                // So, after the previous breakpoint, we have a new breakpoint
                // location starting with , '{number="some number", ...
                // Let's parse that.
                IDebugger::Breakpoint sub_bp;
                if (!parse_breakpoint_with_one_loc (cur, cur,
                                                    /*is_sub_breakpoint=*/true,
                                                    sub_bp)) {
                    LOG_PARSING_ERROR (cur);
                    return false;
                }
                a_bkpt.append_sub_breakpoint (sub_bp);
                continue;
            }
        }
        a_to = saved_cur;
        break;
    }
    return true;
}

bool
GDBMIParser::parse_breakpoint_table (UString::size_type a_from,
                                     UString::size_type &a_to,
                                     map<string, IDebugger::Breakpoint> &a_breakpoints)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur=a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_BREAKPOINT_TABLE),
                                      PREFIX_BREAKPOINT_TABLE)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += strlen (PREFIX_BREAKPOINT_TABLE);
    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    //skip table headers and go to table body.
    cur = RAW_INPUT.find ("body=[", 0);
    if (!cur) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += 6;
    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    map<string, IDebugger::Breakpoint> breakpoint_table;
    if (RAW_CHAR_AT (cur) == ']') {
        //there are zero breakpoints ...
    } else if (!RAW_INPUT.compare (cur, strlen (PREFIX_BKPT),
                                              PREFIX_BKPT)) {
        //there are some breakpoints
        IDebugger::Breakpoint breakpoint;
        while (true) {
            if (RAW_INPUT.compare (cur, strlen (PREFIX_BKPT),
                                              PREFIX_BKPT)) {
                break;
            }
            if (!parse_breakpoint (cur, cur, breakpoint)) {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            breakpoint_table[breakpoint.id ()] = breakpoint;
            if (RAW_CHAR_AT (cur) == ',') {
                ++cur;
                if (m_priv->index_passed_end (cur)) {
                    LOG_PARSING_ERROR (cur);
                    return false;
                }
            }
            breakpoint.clear ();
        }
        if (breakpoint_table.empty ()) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
    } else {
        //weird things are happening, get out.
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (RAW_CHAR_AT (cur) != ']') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;
    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (RAW_CHAR_AT (cur) != '}') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;

    a_to = cur;
    a_breakpoints = breakpoint_table;
    return true;
}

bool
GDBMIParser::parse_breakpoint_modified_async_output (UString::size_type a_from,
                                                     UString::size_type &a_to,
                                                     IDebugger::Breakpoint &a_b)
{
    LOG_FUNCTION_SCOPE_NORMAL_DD;

    UString::size_type cur = a_from;

    int prefix_len = strlen (PREFIX_BREAKPOINT_MODIFIED_ASYNC_OUTPUT);
    if (RAW_INPUT.compare (cur, prefix_len,
                           PREFIX_BREAKPOINT_MODIFIED_ASYNC_OUTPUT)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    cur += prefix_len;
    PARSING_ERROR_IF_END (cur);

    return parse_breakpoint (cur, a_to, a_b);
}

bool
GDBMIParser::parse_threads_list (UString::size_type a_from,
                                 UString::size_type &a_to,
                                 std::list<int> &a_thread_ids)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_THREAD_IDS),
                           PREFIX_THREAD_IDS)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMIResultSafePtr gdbmi_result;
    std::list<int> thread_ids;
    unsigned int num_threads = 0;

    // We loop, parsing GDB/MI RESULT constructs and ',' until we reach '\n'
    while (true) {
        if (!parse_gdbmi_result (cur, cur, gdbmi_result)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        if (gdbmi_result->variable () == "thread-ids") {
            // We've got a RESULT which variable is "thread-ids", we expect
            // the value to be tuple of RESULTs of the form
            // thread-id="<thread-id>"
            THROW_IF_FAIL (gdbmi_result->value ());
            THROW_IF_FAIL ((gdbmi_result->value ()->content_type ()
                               == GDBMIValue::TUPLE_TYPE)
                           ||
                           (gdbmi_result->value ()->content_type ()
                                == GDBMIValue::EMPTY_TYPE));

            GDBMITupleSafePtr gdbmi_tuple;
            if (gdbmi_result->value ()->content_type ()
                 != GDBMIValue::EMPTY_TYPE) {
                gdbmi_tuple = gdbmi_result->value ()->get_tuple_content ();
                THROW_IF_FAIL (gdbmi_tuple);
            }

            list<GDBMIResultSafePtr> result_list;
            if (gdbmi_tuple) {
                result_list = gdbmi_tuple->content ();
            }
            list<GDBMIResultSafePtr>::const_iterator it;
            int thread_id=0;
            for (it = result_list.begin (); it != result_list.end (); ++it) {
                THROW_IF_FAIL (*it);
                if ((*it)->variable () != "thread-id") {
                    LOG_ERROR ("expected a gdbmi value with "
                               "variable name 'thread-id'. "
                               "Got '" << (*it)->variable () << "'");
                    return false;
                }
                THROW_IF_FAIL ((*it)->value ()
                               && ((*it)->value ()->content_type ()
                                   == GDBMIValue::STRING_TYPE));
                thread_id = atoi ((*it)->value ()->get_string_content ()
                                  .c_str ());
                THROW_IF_FAIL (thread_id);
                thread_ids.push_back (thread_id);
            }
        } else if (gdbmi_result->variable () == "number-of-threads") {
            // We've got a RESULT which variable is "number-of-threads",
            // we expect the result to be a string which is the number 
            // of threads.
            THROW_IF_FAIL (gdbmi_result->value ()
                           && gdbmi_result->value ()->content_type ()
                           == GDBMIValue::STRING_TYPE);
            num_threads =
                atoi (gdbmi_result->value ()->get_string_content ().c_str ());
        } else if (gdbmi_result->variable () == "current-thread-id") {
            // If we've got a RESULT which variable is
            // "current-thread-id", expect the result to be a string
            // which is the id of the current thread.  Also, we don't
            // use the current ID for now, as, each time we stop, the
            // current thread id is given in the information related
            // to the stop state.

            // unsigned current_thread_id =
            //    atoi (gdbmi_result->value ()->get_string_content ().c_str ());
        } else {
            // Let's consume the unknown RESULT which we might have gotten for
            // now.
            LOG_ERROR ("Got an unknown RESULT which variable is '"
                       << gdbmi_result->variable ()
                       << "'. Ignoring it thus.");
        }
        SKIP_BLANK (cur);
        if (RAW_CHAR_AT (cur) == '\n') {
            break;
        }
        if (RAW_CHAR_AT (cur) == ',') {
            ++cur;
            CHECK_END (cur);
        }
        SKIP_BLANK (cur);
    }

    if (num_threads != thread_ids.size ()) {
        LOG_ERROR ("num_threads: '"
                   << (int) num_threads
                   << "', thread_ids.size(): '"
                   << (int) thread_ids.size ()
                   << "'");
        return false;
    }

    a_thread_ids = thread_ids;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_new_thread_id (UString::size_type a_from,
                                  UString::size_type &a_to,
                                  int &a_thread_id,
                                  IDebugger::Frame &a_frame)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_NEW_THREAD_ID),
                           PREFIX_NEW_THREAD_ID)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    GDBMIResultSafePtr gdbmi_result;
    if (!parse_gdbmi_result (cur, cur, gdbmi_result)
        || !gdbmi_result) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (gdbmi_result->variable () != "new-thread-id") {
        LOG_ERROR ("expected 'new-thread-id', got '"
                   << gdbmi_result->variable () << "'");
        return false;
    }
    THROW_IF_FAIL (gdbmi_result->value ());
    THROW_IF_FAIL (gdbmi_result->value ()->content_type ()
                   == GDBMIValue::STRING_TYPE);
    CHECK_END (cur);

    int thread_id =
        atoi (gdbmi_result->value ()->get_string_content ().c_str ());
    if (!thread_id) {
        LOG_ERROR ("got null thread id");
        return false;
    }

    SKIP_BLANK (cur);

    if (RAW_CHAR_AT (cur) != ',') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;
    CHECK_END (cur);

    IDebugger::Frame frame;
    if (!parse_frame (cur, cur, frame)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    a_to = cur;
    a_thread_id = thread_id;
    a_frame = frame;
    return true;
}

bool
GDBMIParser::parse_file_list (UString::size_type a_from,
                              UString::size_type &a_to,
                              std::vector<UString> &a_files)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_FILES), PREFIX_FILES)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += 7;

    std::vector<GDBMITupleSafePtr> tuples;
    while (!m_priv->index_passed_end (cur)) {
        GDBMITupleSafePtr tuple;
        if (!parse_gdbmi_tuple (cur, cur, tuple)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        tuples.push_back(tuple);
        if (RAW_CHAR_AT (cur) == ',') {
            ++cur;
        } else if (RAW_CHAR_AT (cur) == ']') {
            //at the end of the list, just get out
            break;
        } else {
            //unexpected data
            LOG_PARSING_ERROR (cur);
        }
    }

    std::vector<UString> files;
    vector<GDBMITupleSafePtr>::const_iterator file_iter;
    for (file_iter = tuples.begin (); file_iter != tuples.end (); ++file_iter) {
        UString filename;
        list<GDBMIResultSafePtr>::const_iterator attr_it;
        for (attr_it = (*file_iter)->content ().begin ();
             attr_it != (*file_iter)->content ().end (); ++attr_it) {
             THROW_IF_FAIL ((*attr_it)->value ()
                           && ((*attr_it)->value ()->content_type ()
                               == GDBMIValue::STRING_TYPE));
            if ((*attr_it)->variable () == "file") {
                //only use the 'file' attribute if the
                //fullname isn't already set
                //FIXME: do we even want to list these at all?
                if (filename.empty ()) {
                    filename = (*attr_it)->value ()->get_string_content ();
                }
            } else if ((*attr_it)->variable () == "fullname") {
                //use the fullname attribute,
                //overwriting the 'file' attribute
                //if necessary
                filename = (*attr_it)->value ()->get_string_content ();
            } else {
                LOG_ERROR ("expected a gdbmi value with "
                            "variable name 'file' or 'fullname'"
                            ". Got '" << (*attr_it)->variable () << "'");
                return false;
            }
        }
        THROW_IF_FAIL (!filename.empty());
        files.push_back (filename);
    }

    std::sort (files.begin(), files.end(), QuickUStringLess());
    std::vector<UString>::iterator last_unique =
        std::unique (files.begin (), files.end ());
    a_files = std::vector<UString> (files.begin (), last_unique);
    a_to = cur;
    LOG_D ("Number of resulting files: "
           << (int) a_files.size (),
           GDBMI_OUTPUT_DOMAIN);
    return true;
}

bool
GDBMIParser::parse_call_stack (const UString::size_type a_from,
                               UString::size_type &a_to,
                               vector<IDebugger::Frame> &a_stack)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    GDBMIResultSafePtr result;
    if (!parse_gdbmi_result (cur, cur, result)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    THROW_IF_FAIL (result);

    if (result->variable () != "stack") {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (!result->value ()
        ||result->value ()->content_type ()
                != GDBMIValue::LIST_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMIListSafePtr result_value_list =
        result->value ()->get_list_content ();
    if (!result_value_list) {
        a_to = cur;
        a_stack.clear ();
        return true;
    }

    if (result_value_list->content_type () != GDBMIList::RESULT_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    list<GDBMIResultSafePtr> result_list;
    result_value_list->get_result_content (result_list);

    GDBMITupleSafePtr frame_tuple;
    vector<IDebugger::Frame> stack;
    list<GDBMIResultSafePtr>::const_iterator iter, frame_part_iter;
    UString value;
    for (iter = result_list.begin (); iter != result_list.end (); ++iter) {
        if (!(*iter)) {continue;}
        THROW_IF_FAIL ((*iter)->value ()
                       && (*iter)->value ()->content_type ()
                       == GDBMIValue::TUPLE_TYPE);

        frame_tuple = (*iter)->value ()->get_tuple_content ();
        THROW_IF_FAIL (frame_tuple);
        IDebugger::Frame frame;
        for (frame_part_iter = frame_tuple->content ().begin ();
             frame_part_iter != frame_tuple->content ().end ();
             ++frame_part_iter) {
            THROW_IF_FAIL ((*frame_part_iter)->value ());
            value = (*frame_part_iter)->value ()->get_string_content ();
            if ((*frame_part_iter)->variable () == "addr") {
                frame.address () = value.raw ();
            } else if ((*frame_part_iter)->variable () == "func") {
                frame.function_name (value.raw ());
            } else if ((*frame_part_iter)->variable () == "file") {
                frame.file_name (value);
            } else if ((*frame_part_iter)->variable () == "fullname") {
                frame.file_full_name (value);
            } else if ((*frame_part_iter)->variable () == "line") {
                frame.line (atol (value.c_str ()));
            } else if ((*frame_part_iter)->variable () == "level") {
                frame.level (atol (value.c_str ()));
            } else if ((*frame_part_iter)->variable () == "from") {
                frame.library (value.c_str ());
	    }
        }
        THROW_IF_FAIL (frame.has_empty_address () != true);
        stack.push_back (frame);
        frame.clear ();
    }
    a_stack = stack;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_local_var_list (UString::size_type a_from,
                                   UString::size_type &a_to,
                                   list<IDebugger::VariableSafePtr> &a_vars)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_INPUT.compare (cur, strlen (PREFIX_LOCALS), PREFIX_LOCALS)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMIResultSafePtr gdbmi_result;
    if (!parse_gdbmi_result (cur, cur, gdbmi_result)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    THROW_IF_FAIL (gdbmi_result
                   && gdbmi_result->variable () == "locals");

    if (!gdbmi_result->value ()
        || gdbmi_result->value ()->content_type ()
            != GDBMIValue::LIST_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMIListSafePtr gdbmi_list =
        gdbmi_result->value ()->get_list_content ();
    if (!gdbmi_list
        || gdbmi_list->content_type () == GDBMIList::UNDEFINED_TYPE) {
        a_to = cur;
        a_vars.clear ();
        return true;
    }
    RETURN_VAL_IF_FAIL (gdbmi_list->content_type () == GDBMIList::VALUE_TYPE,
                        false);

    std::list<GDBMIValueSafePtr> gdbmi_value_list;
    gdbmi_list->get_value_content (gdbmi_value_list);
    RETURN_VAL_IF_FAIL (!gdbmi_value_list.empty (), false);

    std::list<IDebugger::VariableSafePtr> variables;
    std::list<GDBMIValueSafePtr>::const_iterator value_iter;
    std::list<GDBMIResultSafePtr> tuple_content;
    std::list<GDBMIResultSafePtr>::const_iterator tuple_iter;
    for (value_iter = gdbmi_value_list.begin ();
         value_iter != gdbmi_value_list.end ();
         ++value_iter) {
        if (!(*value_iter)) {continue;}
        if ((*value_iter)->content_type () != GDBMIValue::TUPLE_TYPE) {
            LOG_ERROR_D ("list of tuple should contain only tuples",
                         GDBMI_PARSING_DOMAIN);
            continue;
        }
        GDBMITupleSafePtr gdbmi_tuple = (*value_iter)->get_tuple_content ();
        RETURN_VAL_IF_FAIL (gdbmi_tuple, false);
        RETURN_VAL_IF_FAIL (!gdbmi_tuple->content ().empty (), false);

        tuple_content.clear ();
        tuple_content = gdbmi_tuple->content ();
        RETURN_VAL_IF_FAIL (!tuple_content.empty (), false);
        IDebugger::VariableSafePtr variable (new IDebugger::Variable);
        for (tuple_iter = tuple_content.begin ();
             tuple_iter != tuple_content.end ();
             ++tuple_iter) {
            if (!(*tuple_iter)) {
                LOG_ERROR_D ("got and empty tuple member",
                             GDBMI_PARSING_DOMAIN);
                continue;
            }

            if (!(*tuple_iter)->value ()
                ||(*tuple_iter)->value ()->content_type ()
                    != GDBMIValue::STRING_TYPE) {
                LOG_ERROR_D ("Got a tuple member which value is not a string",
                             GDBMI_PARSING_DOMAIN);
                continue;
            }

            UString variable_str = (*tuple_iter)->variable ();
            UString value_str =
                        (*tuple_iter)->value ()->get_string_content ();
            value_str.chomp ();
            if (variable_str == "name") {
                variable->name (value_str);
            } else if (variable_str == "type") {
                variable->type (value_str);
            } else if (variable_str == "value") {
                variable->value (value_str);
            } else {
                LOG_ERROR_D ("got an unknown tuple member with name: '"
                             << variable_str << "'",
                             GDBMI_PARSING_DOMAIN);
                continue;
            }

        }
        variables.push_back (variable);
    }

    LOG_D ("got '" << (int)variables.size () << "' variables",
           GDBMI_PARSING_DOMAIN);

    a_vars = variables;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_stack_arguments (UString::size_type a_from,
                                    UString::size_type &a_to,
                                    map<int, list<IDebugger::VariableSafePtr> >
                                                                            &a_params)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_INPUT.compare (cur, strlen (PREFIX_STACK_ARGS),
                           PREFIX_STACK_ARGS)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMIResultSafePtr gdbmi_result;
    if (!parse_gdbmi_result (cur, cur, gdbmi_result)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    THROW_IF_FAIL (gdbmi_result
                   && gdbmi_result->variable () == "stack-args");

    if (!gdbmi_result->value ()
        || gdbmi_result->value ()->content_type ()
            != GDBMIValue::LIST_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMIListSafePtr gdbmi_list =
        gdbmi_result->value ()->get_list_content ();
    if (!gdbmi_list) {
        a_to = cur;
        a_params.clear ();
        return true;
    }

    if (gdbmi_list->content_type () != GDBMIList::RESULT_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    list<GDBMIResultSafePtr> frames_params_list;
    gdbmi_list->get_result_content (frames_params_list);
    LOG_D ("number of frames: " << (int) frames_params_list.size (),
           GDBMI_PARSING_DOMAIN);

    list<GDBMIResultSafePtr>::const_iterator frames_iter,
                                        params_records_iter,
                                        params_iter;
    map<int, list<IDebugger::VariableSafePtr> > all_frames_args;
    //walk through the list of frames
    //each frame is a tuple of the form:
    //{level="2", args=[list-of-arguments]}
    for (frames_iter = frames_params_list.begin ();
         frames_iter != frames_params_list.end ();
         ++frames_iter) {
        if (!(*frames_iter)) {
            LOG_D ("Got a null frmae, skipping", GDBMI_PARSING_DOMAIN);
            continue;
        }
        THROW_IF_FAIL ((*frames_iter)->variable () != "stack");
        THROW_IF_FAIL ((*frames_iter)->value ()
                        && (*frames_iter)->value ()->content_type ()
                        == GDBMIValue::TUPLE_TYPE)

        //params_record is a tuple that has the form:
        //{level="2", args=[list-of-arguments]}
        GDBMITupleSafePtr params_record;
        params_record = (*frames_iter)->value ()->get_tuple_content ();
        THROW_IF_FAIL (params_record);

        //walk through the tuple {level="2", args=[list-of-arguments]}
        int cur_frame_level=-1;
        for (params_records_iter = params_record->content ().begin ();
             params_records_iter != params_record->content ().end ();
             ++params_records_iter) {
            THROW_IF_FAIL ((*params_records_iter)->value ());

            if ((*params_records_iter)->variable () == "level") {
                THROW_IF_FAIL
                ((*params_records_iter)->value ()
                 && (*params_records_iter)->value ()->content_type ()
                 == GDBMIValue::STRING_TYPE);
                cur_frame_level = atoi
                    ((*params_records_iter)->value
                         ()->get_string_content ().c_str ());
                LOG_D ("frame level '" << (int) cur_frame_level << "'",
                       GDBMI_PARSING_DOMAIN);
            } else if ((*params_records_iter)->variable () == "args") {
                //this gdbmi result is of the form:
                //args=[{name="foo0", value="bar0"},
                //      {name="foo1", bar="bar1"}]

                THROW_IF_FAIL
                ((*params_records_iter)->value ()
                 && (*params_records_iter)->value ()->get_list_content ());

                GDBMIListSafePtr arg_list =
                    (*params_records_iter)->value ()->get_list_content ();
                list<GDBMIValueSafePtr>::const_iterator args_as_value_iter;
                list<IDebugger::VariableSafePtr> cur_frame_args;
                if (arg_list && !(arg_list->empty ())) {
                    LOG_D ("arg list is *not* empty for frame level '"
                           << (int)cur_frame_level,
                           GDBMI_PARSING_DOMAIN);
                    //walk each parameter.
                    //Each parameter is a tuple (in a value)
                    list<GDBMIValueSafePtr> arg_as_value_list;
                    arg_list->get_value_content (arg_as_value_list);
                    LOG_D ("arg list size: "
                           << (int)arg_as_value_list.size (),
                           GDBMI_PARSING_DOMAIN);
                    for (args_as_value_iter=arg_as_value_list.begin();
                         args_as_value_iter!=arg_as_value_list.end();
                         ++args_as_value_iter) {
                        if (!*args_as_value_iter) {
                            LOG_D ("got NULL arg, skipping",
                                   GDBMI_PARSING_DOMAIN);
                            continue;
                        }
                        GDBMITupleSafePtr args =
                            (*args_as_value_iter)->get_tuple_content ();
                        list<GDBMIResultSafePtr>::const_iterator arg_iter;
                        IDebugger::VariableSafePtr parameter
                                                (new IDebugger::Variable);
                        THROW_IF_FAIL (parameter);
                        THROW_IF_FAIL (args);
                        //walk the name and value of the parameter
                        for (arg_iter = args->content ().begin ();
                             arg_iter != args->content ().end ();
                             ++arg_iter) {
                            THROW_IF_FAIL (*arg_iter);
                            if ((*arg_iter)->variable () == "name") {
                                THROW_IF_FAIL ((*arg_iter)->value ());
                                parameter->name
                                ((*arg_iter)->value()->get_string_content ());
                            } else if ((*arg_iter)->variable () == "value") {
                                THROW_IF_FAIL ((*arg_iter)->value ());
                                parameter->value
                                    ((*arg_iter)->value
                                                 ()->get_string_content());
                                UString::size_type pos;
                                pos = parameter->value ().find ("{");
                                if (pos != Glib::ustring::npos) {
                                    //in certain cases
                                    //(gdbmi is not strict enough to be sure)
                                    //having a '{' in the parameter value
                                    //means the parameter has an anonymous
                                    //member variable. So let's try to
                                    //parse an anonymous member variable
                                    //and in case of success,
                                    //fill parameter->members()
                                    //with the structured member
                                    //embedded in parameter->value()
                                    //and set parameter->value() to nothing
                                    //This is shity performancewise
                                    //(and is ugly) but that's the way
                                    //of gdbmi
                                    push_input (parameter->value ());
                                    if (parse_member_variable (pos, pos,
                                                               parameter,
                                                               true)) {
                                        parameter->value ("");
                                    } else {
                                        LOG_ERROR ("Oops, '{' was not for "
                                                   "for an embedded variable");
                                    }
                                    pop_input ();
                                }
                            } else {
                                THROW ("should not reach this line");
                            }
                        }
                        LOG_D ("pushing arg '"
                               <<parameter->name()
                               <<"'='"<<parameter->value() <<"'"
                               <<" for frame level='"
                               <<(int)cur_frame_level
                               <<"'",
                               GDBMI_PARSING_DOMAIN);
                        cur_frame_args.push_back (parameter);
                    }
                } else {
                    LOG_D ("arg list is empty for frame level '"
                           << (int)cur_frame_level,
                           GDBMI_PARSING_DOMAIN);
                }
                THROW_IF_FAIL (cur_frame_level >= 0);
                LOG_D ("cur_frame_level: '"
                       << (int) cur_frame_level
                       << "', NB Params: "
                       << (int) cur_frame_args.size (),
                       GDBMI_PARSING_DOMAIN);
                all_frames_args[cur_frame_level] = cur_frame_args;
            } else {
                LOG_PARSING_ERROR (cur);
                return false;
            }
        }

    }

    a_to = cur;
    a_params = all_frames_args;
    LOG_D ("number of frames parsed: " << (int)a_params.size (),
           GDBMI_PARSING_DOMAIN);
    return true;
}

bool
GDBMIParser::parse_member_variable (const UString::size_type a_from,
                                    UString::size_type &a_to,
                                    IDebugger::VariableSafePtr &a_var,
                                    bool a_in_unnamed_var)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    LOG_D ("in_unnamed_var = " <<(int)a_in_unnamed_var, GDBMI_PARSING_DOMAIN);
    THROW_IF_FAIL (a_var);

    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_CHAR_AT (cur) != '{') {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    ++cur;
    CHECK_END (cur);

    UString name, value;
    UString::size_type name_start=0, name_end=0, value_start=0, value_end=0;

    while (true /*fetch name*/) {
        name_start=0, name_end=0, value_start=0, value_end=0;
        name = "#unnamed#" , value = "";

        SKIP_BLANK (cur);
        LOG_D ("fetching name ...", GDBMI_PARSING_DOMAIN);
        //we should be at the begining of A = B. lets try to parse A
        if (RAW_CHAR_AT (cur) != '{') {
            SKIP_BLANK (cur);
            name_start = cur;
            while (true) {
                if (!m_priv->index_passed_end (cur)
                    && RAW_CHAR_AT (cur) != '='
                    && RAW_CHAR_AT (cur) != '}') {
                    ++cur;
                } else {
                    //if we found an '}' character, make sure
                    //it is not enclosed in sigle quotes. If it is in
                    //single quotes then ignore it.
                    if (cur > 0
                        && !m_priv->index_passed_end (cur+1)
                        && RAW_CHAR_AT (cur-1) == '\''
                        && RAW_CHAR_AT (cur+1) == '\'') {
                        ++cur;
                        continue;
                    }
                    break;
                }
            }
            //we should be at the end of A (as in A = B)
            name_end = cur - 1;
            name.assign (RAW_INPUT, name_start, name_end - name_start + 1);
            LOG_D ("got name '" << name << "'", GDBMI_PARSING_DOMAIN);
        }

        IDebugger::VariableSafePtr cur_var (new IDebugger::Variable);
        name.chomp ();
        cur_var->name (name);

        if (RAW_CHAR_AT (cur) == '}') {
            ++cur;
            cur_var->value ("");
            a_var->append (cur_var);
            LOG_D ("reached '}' after '"
                   << name << "'",
                   GDBMI_PARSING_DOMAIN);
            break;
        }

        SKIP_BLANK (cur);
        if (RAW_CHAR_AT (cur) != '{') {
            ++cur;
            CHECK_END (cur);
            SKIP_BLANK (cur);
        }

        //if we are at a '{', (like in A = {B}),
        //the B is said to be a member variable of A
        //you can also call it a member attribute of A
        //In this case, let's try to parse {B}
        if (RAW_CHAR_AT (cur) == '{') {
            bool in_unnamed = true;
            if (name == "#unnamed#") {
                in_unnamed = false;
            }
            if (!parse_member_variable (cur, cur, cur_var, in_unnamed)) {
                LOG_PARSING_ERROR (cur);
                return false;
            }
        } else {
            //else, we are at a B, (like in A = B),
            //so let's try to parse B
            SKIP_BLANK (cur);
            value_start = cur;
            unsigned int brace_level = 0;
            while (true) {
                UString str;
                if ( RAW_CHAR_AT (cur) == '"'
                    && RAW_CHAR_AT (cur -1) != '\\') {
                    if (!parse_c_string (cur, cur, str)) {
                        LOG_PARSING_ERROR (cur);
                        return false;
                    }
                } else if (!m_priv->index_passed_end (cur+2)
                           && RAW_CHAR_AT (cur) == '\\'
                           && RAW_CHAR_AT (cur+1) == '"'
                           && RAW_CHAR_AT (cur+2) != '\''
                           && RAW_CHAR_AT (cur-1) != '\'') {
                    if (!parse_embedded_c_string (cur, cur, str)){
                        LOG_PARSING_ERROR (cur);
                        return false;
                    }
                } else if ((RAW_CHAR_AT (cur) != ','
                               || ((!m_priv->index_passed_end (cur+1))
                                   && RAW_CHAR_AT (cur+1) != ' ')
                               // don't treat a comma inside braces as a
                               // variable separator.  e.g the following output:
                               // "0x40085e <my_func(void*, void*)>"
                               || (brace_level > 0))
                           && RAW_CHAR_AT (cur) != '}') {
                    if (RAW_CHAR_AT (cur) == '<') {
                        ++brace_level;
                    } else if (RAW_CHAR_AT (cur) == '>') {
                        --brace_level;
                    }
                    ++cur;
                    CHECK_END (cur);
                } else {
                    //if we found an '}' character, make sure
                    //it is not enclosed in sigle quotes. If it is in
                    //single quotes then ignore it.
                    if (cur > 0
                        && !m_priv->index_passed_end (cur+1)
                        && RAW_CHAR_AT (cur-1) == '\''
                        && RAW_CHAR_AT (cur+1) == '\'') {
                        ++cur;
                        continue;
                    }
                    //otherwise, getting out condition is either
                    //", " or "}".  check out the the 'if' condition.
                    break;
                }
            }
            if (cur != value_start) {
                value_end = cur - 1;
                value.assign (RAW_INPUT, value_start,
                              value_end - value_start + 1);
                LOG_D ("got value: '"
                       << value << "'",
                       GDBMI_PARSING_DOMAIN);
            } else {
                value = "";
                LOG_D ("got empty value", GDBMI_PARSING_DOMAIN);
            }
            cur_var->value (value);
        }
        a_var->append (cur_var);

        LOG_D ("cur char: " << (char) RAW_CHAR_AT (cur),
               GDBMI_PARSING_DOMAIN);

end_of_block:
        SKIP_BLANK (cur);

        LOG_D ("cur char: " << (char) RAW_CHAR_AT (cur),
                GDBMI_PARSING_DOMAIN);

        if (m_priv->index_passed_end (cur) || RAW_CHAR_AT (cur) == '"') {
            break;
        } else if (RAW_CHAR_AT (cur) == '}') {
            ++cur;
            break;
        } else if (RAW_CHAR_AT (cur) == ',') {
            ++cur;
            CHECK_END (cur);
            LOG_D ("got ',' , going to fetch name",
                   GDBMI_PARSING_DOMAIN);
            continue /*goto fetch name*/;
        } else if (!RAW_INPUT.compare (cur, 9, "<repeats ")) {
            //TODO: we need to handle the <repeat N> format at some point.
            //just skipping it for now.
            bool skipped_repeat=false;
            while (true) {
                ++cur;
                if (m_priv->index_passed_end (cur)) {break;}
                if (RAW_CHAR_AT (cur) == '>') {
                    ++cur;
                    skipped_repeat = true;
                    break;
                }
            }
            if (skipped_repeat) {
                LOG_D ("skipped repeat construct", GDBMI_PARSING_DOMAIN);
                goto end_of_block;
                break;
            } else {
                LOG_ERROR ("failed to skip repeat construct");
                LOG_PARSING_ERROR (cur);
                return false;
            }
        } else if (!RAW_INPUT.compare (cur, 3, "...")) {
            //hugh ? wtf does this '...' mean ? Anyway, skip it for now.
            cur+= 3;
            goto end_of_block;
        }
        LOG_PARSING_ERROR (cur);
        THROW ("should not be reached");
    }//end while

    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_variable_value (const UString::size_type a_from,
                                   UString::size_type &a_to,
                                   IDebugger::VariableSafePtr &a_var)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_INPUT.compare (cur, strlen (PREFIX_VALUE), PREFIX_VALUE)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    cur += 6;
    CHECK_END (cur);
    CHECK_END (cur+1);

    a_var = IDebugger::VariableSafePtr (new IDebugger::Variable);
    if (RAW_CHAR_AT (cur+1) == '{') {
        ++cur;
        if (!parse_member_variable (cur, cur, a_var)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        SKIP_BLANK (cur);
        if (RAW_CHAR_AT (cur) != '"') {
            UString value;
            if (!parse_c_string_body (cur, cur, value)) {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            value = a_var->value () + " " + value;
            a_var->value (value);
        }
    } else {
        UString value;
        if (!parse_c_string (cur, cur, value)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        a_var->value (value);
    }

    ++cur;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_variables_deleted (UString::size_type a_from,
                                      UString::size_type &a_to,
                                      unsigned int &a_nb_vars_deleted)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_INPUT.compare (cur, strlen (PREFIX_VARIABLE_DELETED),
                           PREFIX_VARIABLE_DELETED)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    GDBMIResultSafePtr result;
    if (!parse_gdbmi_result (cur, cur, result) || !result) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (result->variable () != NDELETED) {
        LOG_ERROR ("expected gdbmi variable " << NDELETED << ", got: "
                   << result->variable () << "\'");
        return false;
    }
    if (!result->value ()
        || result->value ()->content_type () != GDBMIValue::STRING_TYPE) {
        LOG_ERROR ("expected a string value for "
                   "the GDBMI variable " << NDELETED);
        return false;
    }
    UString val = result->value ()->get_string_content ();
    a_nb_vars_deleted = val.empty () ? 0 : atoi (val.c_str ());
    a_to = cur;
    return true;
}

// We want to parse something like:
// 'numchild=N,children=[{name=NAME,numchild=N,type=TYPE}]'
// It's actually two RESULTs, separated by a comma. The second
// result is a LIST of TUPLEs. Each TUPLE represents a children variable.
bool
GDBMIParser::parse_var_list_children
                            (UString::size_type a_from,
                             UString::size_type &a_to,
                             std::vector<IDebugger::VariableSafePtr> &a_vars)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_INPUT.compare (cur, strlen (PREFIX_NUMCHILD),
                           PREFIX_NUMCHILD)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    GDBMIResultSafePtr result;
    if (!parse_gdbmi_result (cur, cur, result) || !result) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (result->variable () != NUMCHILD) {
        LOG_ERROR ("expected gdbmi variable " << NUMCHILD<< ", got: "
                   << result->variable () << "\'");
        return false;
    }
    if (!result->value ()
        || result->value ()->content_type () != GDBMIValue::STRING_TYPE) {
        LOG_ERROR ("expected a string value for "
                   "the GDBMI variable " << NUMCHILD);
        return false;
    }
    UString s = result->value ()->get_string_content ();
    unsigned num_children = s.empty () ? 0 : atoi (s.c_str ());

    if (!num_children) {
        LOG_D ("Variable has zero children",
               GDBMI_PARSING_DOMAIN);
        a_to = cur;
        return true;
    }

    SKIP_BLANK (cur);
    if (RAW_CHAR_AT (cur) != ',') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    ++cur;

    // Go look for the "children" RESULT, and ignore the other ones
    // we might encounter.
    for (;;) {
        SKIP_BLANK (cur);
        if (RAW_CHAR_AT (cur) == ',') {
            ++cur;
            SKIP_BLANK (cur);
        }
        result.reset ();
        if (!parse_gdbmi_result (cur, cur, result) || !result) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        if (result->variable () == "children")
            break;
    }

    if (result->variable () != "children") {
        LOG_ERROR ("expected gdbmi variable " << "children" << ", got: "
                   << result->variable () << "\'");
        return false;
    }
    if (!result->value ()
        || result->value ()->content_type () != GDBMIValue::LIST_TYPE) {
        LOG_ERROR ("expected a LIST value for "
                   "the GDBMI variable " << "children");
        return false;
    }
    GDBMIListSafePtr children = result->value ()->get_list_content ();
    THROW_IF_FAIL (children);

    if (children->empty ()) {
        a_to = cur;
        return true;
    }
    if (children->content_type () != GDBMIList::RESULT_TYPE) {
        LOG_ERROR ("Expected the elements of the 'children' LIST to be "
                   "of kind RESULT");
        return false;
    }
    list<GDBMIResultSafePtr> children_results;
    children->get_result_content (children_results);

    if (children_results.empty ()) {
        a_to = cur;
        return true;
    }

    // Walk the children variables, that are packed in the children_result
    // list of RESULT. Each RESULT represents a variable, that is a child
    // of a_var.
    typedef list<GDBMIResultSafePtr>::const_iterator ResultsIter;
    for (ResultsIter result_it = children_results.begin ();
         result_it != children_results.end ();
         ++result_it) {
        if (!*result_it) {
            LOG_ERROR ("Got NULL RESULT in the list of children variables.");
            continue;
        }
        if ((*result_it)->variable () != "child") {
            LOG_ERROR ("Expected the name of the child result to be 'child'."
                       "Got " << (*result_it)->variable ());
            return false;
        }
        if ((*result_it)->value ()->content_type () != GDBMIValue::TUPLE_TYPE) {
            LOG_ERROR ("Values of the 'child' result must be a kind TYPE");
            return false;
        }
        // The components of a given variable result_it are packed into
        // the list of RESULT below.
        list<GDBMIResultSafePtr> child_comps =
            (*result_it)->value ()->get_tuple_content ()->content ();

        // Walk the list of the components of the current child of a_var.
        // At this end of this walk, we will be able to build a child
        // of a_var, instance of IDebugger::Variable.
        UString s, v, name, internal_name, value, type;
        unsigned int numchildren = 0;
        for (ResultsIter it = child_comps.begin ();
             it != child_comps.end ();
             ++it) {
            if (!(*it)) {
                LOG_ERROR ("Got a NULL RESULT as a variable component");
                continue;
            }
            v = (*it)->variable ();
            s = (*it)->value ()->get_string_content ();
            if (v == "name") {
                if ((*it)->value ()->content_type ()
                    != GDBMIValue::STRING_TYPE) {
                    LOG_ERROR ("Variable internal name should "
                               "have a string value");
                    continue;
                }
                internal_name = s;
            } else if (v == "exp") {
                if ((*it)->value ()->content_type ()
                    != GDBMIValue::STRING_TYPE) {
                    LOG_ERROR ("Variable name should "
                               "have a string value");
                    continue;
                }
                name = s;
            } else if (v == "value") {
                value = s;
            } else if (v == "type") {
                if ((*it)->value ()->content_type ()
                    != GDBMIValue::STRING_TYPE) {
                    LOG_ERROR ("Variable type should "
                               "have a string value");
                    continue;
                }
                type = s;
            } else if (v == "numchild") {
                numchildren = atoi (s.c_str ());
            }
        }
        if (!name.empty ()
            && !internal_name.empty ()
            /* We don't require type to be non empty
             * because members might have empty types. For instance,
             * GDB considers the 'public' or 'protected' keywords of a class
             * to be a member without type.
             * We don't require value to be non empty either, as
             * members that have children won't have any value.  */) {
            IDebugger::VariableSafePtr var
                (new IDebugger::Variable (internal_name,
                                          name, value, type));
            var->num_expected_children (numchildren);
            a_vars.push_back (var);
        }
    }
    a_to = cur;
    return true;
}

/// This function takes a GDB/MI VALUE data structure that contains a
/// LIST of VALUEs representing the list of variables that changed (as
/// a result of the command -var-update --all-values <varobj-name>).
/// Each VALUE in the LIST (so each changed variable) contains a TUPLE
/// representing the components of the variable.
///
/// This function analyses this input and constructs a list of
/// instances of IDebugger::Variable representing the list of changed
/// variables as returned by the -var-udpate command.
///
/// \param a_value the GDB/MI VALUE data structure presented above.
///
/// \param a_vars an output parameter.  The resulting list of
/// variables built by this function.
///
/// \return true upon succesful completion
static bool
grok_var_changed_list_components (GDBMIValueSafePtr a_value,
                                  list<VarChangePtr> &a_var_changes)
{
    // The value of the RESULT must be a LIST
    if (!a_value
        || a_value->content_type () != GDBMIValue::LIST_TYPE
        || !a_value->get_list_content ()) {
        LOG_ERROR ("expected a LIST value for the GDBMI variable "
                   << CHANGELIST);
        return false;
    }

    // Have we got an empty list ?
    if (a_value->get_list_content ()->empty ()) {
        return true;
    }

    if (a_value->get_list_content ()->content_type ()
        != GDBMIList::VALUE_TYPE) {
        LOG_ERROR ("expected a TUPLE content in the LIST value of "
                   << CHANGELIST);
        return false;
    }

    list<GDBMIValueSafePtr> values;
    a_value->get_list_content ()->get_value_content (values);
    if (values.empty ()) {
        LOG_ERROR ("expected a non empty TUPLE content in the LIST value of "
                   << CHANGELIST);
        return false;
    }

    list<VarChangePtr> var_changes;
    for (list<GDBMIValueSafePtr>::const_iterator value_it = values.begin ();
         value_it != values.end ();
         ++value_it) {
        if (!(*value_it)) {
            continue;
        }
        if ((*value_it)->content_type () != GDBMIValue::TUPLE_TYPE
            || !(*value_it)->get_tuple_content ()) {
            LOG_ERROR ("expected a non empty TUPLE content in the LIST value of "
                       << CHANGELIST);
            return false;
        }
        // the components of a given child variable
        list<GDBMIResultSafePtr> comps =
            (*value_it)->get_tuple_content ()->content ();
        UString n, internal_name, value, display_hint, type;
        bool in_scope = true, has_more = false, dynamic = false;
        int new_num_children = -1;
        list<IDebugger::VariableSafePtr> children;
        list<VarChangePtr> sub_var_changes;
        // Walk the list of components of the child variable and really
        // build the damn variable
        for (list<GDBMIResultSafePtr>::const_iterator it = comps.begin ();
             it != comps.end ();
             ++it) {
            if (!(*it)
                || ((*it)->value ()->content_type () != GDBMIValue::STRING_TYPE
                    && (*it)->value ()->content_type () != GDBMIValue::LIST_TYPE)) {
                continue;
            }

            n = (*it)->variable ();
            if ((*it)->value ()->content_type () == GDBMIValue::STRING_TYPE) {
                UString v;
                v = (*it)->value ()->get_string_content ();
                if (n == "name") {
                    internal_name = v;
                } else if (n == "value") {
                    value = v;
                } else if (n == "type") {
                    type = v;
                } else if (n == "in_scope") {
                    in_scope = (v == "true");
                } else if (n == "type_changed") {
                    // type_changed = (v == "true");
                } else if (n == "has_more") {
                    has_more = (v == "0") ? false : true;
                } else if (n == "dynamic") {
                    dynamic = (v == "0") ? false : true;
                } else if (n == "displayhint") {
                    display_hint = v;
                } else if (n == "new_num_children") {
                    new_num_children = atoi (v.c_str ());
                }
            } else if ((*it)->value ()->content_type () == GDBMIValue::LIST_TYPE) {
                if (n == "new_children") {
                    // (it*)->value () should contain one value (a TUPLE) per
                    // child.  Each child containing the components
                    // that appeared as part of the "changed values"
                    // of this variable.  This can happen with a
                    // pretty printed variable like a container into
                    // which new elements got added.
                    grok_var_changed_list_components ((*it)->value (),
                                                      sub_var_changes);
                }
            }
        }
        if (!internal_name.empty ()) {
            IDebugger::VariableSafePtr var;
                var.reset
                    (new IDebugger::Variable (internal_name,
                                              "" /* name */,
                                              value,
                                              type,
                                              in_scope));
            var->is_dynamic (dynamic);
            var->display_hint (display_hint);
            var->has_more_children (has_more);
            // If var has some new children that appeared, append them
            // as member now.
            VarChangePtr var_change (new VarChange);
            var_change->variable (var);
            list<VarChangePtr>::const_iterator i;
            for (i = sub_var_changes.begin ();
                 i != sub_var_changes.end ();
                 ++i) {
                // None of the new sub variables of var should
                // themselves contain any new sub variable.
                THROW_IF_FAIL ((*i)->new_children ().empty ());
                THROW_IF_FAIL ((*i)->new_num_children () == -1);
                var_change->new_children ().push_back ((*i)->variable ());
            }
            var_change->new_num_children (new_num_children);
            var_changes.push_back (var_change);
        }
    }
    a_var_changes = var_changes;
    return true;
}

// We want to parse something that looks likes:
// 'changelist=[{name="var1",value="3",in_scope="true",type_changed="false"}]'
// It's a RESULT, which value is a LIST of TUPLEs.
// Each TUPLE represents a child variable.
bool
GDBMIParser::parse_var_changed_list (UString::size_type a_from,
                                     UString::size_type &a_to,
                                     list<VarChangePtr> &a_var_changes)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_INPUT.compare (cur, strlen (PREFIX_VARIABLES_CHANGED_LIST),
                           PREFIX_VARIABLES_CHANGED_LIST)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    GDBMIResultSafePtr result;
    if (!parse_gdbmi_result (cur, cur, result) || !result) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    // The name of RESULT must be CHANGELIST
    if (result->variable () != CHANGELIST) {
        LOG_ERROR ("expected gdbmi variable " << CHANGELIST << ", got: "
                   << result->variable () << "\'");
        return false;
    }

    a_to = cur;

    return grok_var_changed_list_components (result->value (), a_var_changes);
}

/// Parse the result of -var-info-path-expression.
/// It's basically a RESULT of the form:
/// path_expr="((Base)c).m_size)"
bool
GDBMIParser::parse_var_path_expression (UString::size_type a_from,
                                        UString::size_type &a_to,
                                        UString &a_expression)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_INPUT.compare (cur, strlen (PREFIX_PATH_EXPR),
                           PREFIX_PATH_EXPR)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    GDBMIResultSafePtr result;
    if (!parse_gdbmi_result (cur, cur, result) || !result) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    // The name of RESULT must be PATH_EXPR
    if (result->variable () != PATH_EXPR) {
        LOG_ERROR ("expected gdbmi variable " << PATH_EXPR<< ", got: "
                   << result->variable () << "\'");
        return false;
    }
    // The value of the RESULT must be a STRING
    if (!result->value ()
        || result->value ()->content_type () != GDBMIValue::STRING_TYPE
        || result->value ()->get_string_content ().empty ()) {
        LOG_ERROR ("expected a STRING value for the GDBMI variable "
                   << PATH_EXPR);
        return false;
    }

    a_expression = result->value ()->get_string_content ();
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_variable_format (UString::size_type a_from,
                                    UString::size_type &a_to,
                                    IDebugger::Variable::Format &a_format,
                                    UString &a_value)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;
    CHECK_END (cur);

    if (RAW_INPUT.compare (cur, strlen (PREFIX_VARIABLE_FORMAT),
                           PREFIX_VARIABLE_FORMAT)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    UString name, value;
    if (!parse_gdbmi_string_result (cur, cur, name, value)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    const char *FORMAT = "format";
    if (name != FORMAT) {
        LOG_ERROR ("expected gdbmi variable " << FORMAT << ", got: "
                   << name << "\'");
        return false;
    }

    a_format = debugger_utils::string_to_variable_format (value);
    if (a_format == IDebugger::Variable::UNKNOWN_FORMAT) {
        LOG_ERROR ("got unknown variable format: '" << a_format << "'");
        return false;
    }

    SKIP_WS (cur);
    if (RAW_CHAR_AT (cur) == ',') {
        ++cur;
        SKIP_WS (cur);
        name.clear (), value.clear ();
        if (!parse_gdbmi_string_result (cur, cur, name, value)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        const char *VALUE = "value";
        if (name == VALUE) {
            if (value.empty ()) {
                LOG_ERROR ("the 'value' property should have a non-empty value");
                return true;
            }
            a_value = value;
        }
    }

    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_register_names (UString::size_type a_from,
                                   UString::size_type &a_to,
                                   std::map<IDebugger::register_id_t,
                                   UString> &a_registers)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_REGISTER_NAMES),
                           PREFIX_REGISTER_NAMES)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += strlen (PREFIX_REGISTER_NAMES);

    GDBMIListSafePtr reg_list;
    if (!parse_gdbmi_list (cur, cur, reg_list)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (RAW_CHAR_AT (cur-1) != ']') {
        //unexpected data
        LOG_PARSING_ERROR (cur);
        return false;
    }

    std::map<IDebugger::register_id_t, UString> regs;
    if (reg_list->content_type () != GDBMIList::VALUE_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    std::list<GDBMIValueSafePtr> value_list;
    reg_list->get_value_content (value_list);
    IDebugger::register_id_t id = 0;
    std::list<GDBMIValueSafePtr>::const_iterator val_iter;
    for (val_iter = value_list.begin();
         val_iter != value_list.end();
         ++val_iter, ++id) {
        UString regname = (*val_iter)->get_string_content ();
        regs[id] = regname;
    }

    a_registers = regs;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_changed_registers (UString::size_type a_from,
                                      UString::size_type &a_to,
                                      std::list<IDebugger::register_id_t> &a_registers)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_CHANGED_REGISTERS),
                           PREFIX_CHANGED_REGISTERS)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += strlen (PREFIX_CHANGED_REGISTERS);

    GDBMIListSafePtr reg_list;
    if (!parse_gdbmi_list (cur, cur, reg_list)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (RAW_CHAR_AT (cur-1) != ']') {
        // unexpected data
        LOG_PARSING_ERROR (cur);
        return false;
    }

    std::list<IDebugger::register_id_t> regs;
    if (!reg_list->empty () &&
            reg_list->content_type () != GDBMIList::VALUE_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    std::list<GDBMIValueSafePtr> value_list;
    reg_list->get_value_content (value_list);
    for (std::list<GDBMIValueSafePtr>::const_iterator val_iter =
            value_list.begin ();
            val_iter != value_list.end ();
            ++val_iter) {
        UString regname = (*val_iter)->get_string_content ();
        regs.push_back (atoi (regname.c_str ()));
    }

    a_registers = regs;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_register_values (UString::size_type a_from,
                                    UString::size_type &a_to,
                                    std::map<IDebugger::register_id_t, UString>
                                                                        &a_values)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_REGISTER_VALUES),
                           PREFIX_REGISTER_VALUES)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += strlen (PREFIX_REGISTER_VALUES);

    GDBMIListSafePtr gdbmi_list;
    if (!parse_gdbmi_list (cur, cur, gdbmi_list)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    if (RAW_CHAR_AT (cur-1) != ']') {
        // unexpected data
        LOG_PARSING_ERROR (cur);
        return false;
    }

    std::map<IDebugger::register_id_t, UString> vals;
    if (gdbmi_list->content_type () != GDBMIList::VALUE_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    std::list<GDBMIValueSafePtr> val_list;
    gdbmi_list->get_value_content (val_list);
    std::list<GDBMIValueSafePtr>::const_iterator val_iter;
    for (val_iter = val_list.begin ();
         val_iter != val_list.end ();
         ++val_iter) {
        UString value_str;
        if ((*val_iter)->content_type ()
            != GDBMIValue::TUPLE_TYPE) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        GDBMITupleSafePtr tuple = (*val_iter)->get_tuple_content ();
        std::list<GDBMIResultSafePtr> result_list = tuple->content ();
        if (result_list.size () != 2) {
            // each tuple should have a 'number' and 'value' field
            LOG_PARSING_ERROR (cur);
            return false;
        }
        std::list<GDBMIResultSafePtr>::const_iterator res_iter =
                                                result_list.begin ();
        // get register number
        GDBMIValueSafePtr reg_number_val = (*res_iter)->value ();
        if ((*res_iter)->variable () != "number"
            || reg_number_val->content_type () != GDBMIValue::STRING_TYPE) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        IDebugger::register_id_t id =
                atoi (reg_number_val->get_string_content ().c_str ());

        // get the new value of the register
        ++res_iter;
        GDBMIValueSafePtr reg_value_val = (*res_iter)->value ();
        if ((*res_iter)->variable () != "value"
            || reg_value_val->content_type () != GDBMIValue::STRING_TYPE) {
            LOG_PARSING_ERROR (cur);
            return false;
        } else {
            value_str = reg_value_val->get_string_content ();
        }
        vals[id] = value_str;
    }

    a_values = vals;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_memory_values (UString::size_type a_from,
                                  UString::size_type &a_to,
                                  size_t& a_start_addr,
                                  std::vector<uint8_t> &a_values)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_MEMORY_VALUES),
                           PREFIX_MEMORY_VALUES)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    //skip to the actual list of memory values
    const char* prefix_memory = "memory=";
    cur = RAW_INPUT.find (prefix_memory);
    if (!cur) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    cur += strlen (prefix_memory);

    GDBMIListSafePtr mem_gdbmi_list;
    if (!parse_gdbmi_list (cur, cur, mem_gdbmi_list)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (RAW_CHAR_AT (cur-1) != ']') {
        //unexpected data
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (mem_gdbmi_list->content_type ()
        != GDBMIList::VALUE_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    std::list<GDBMIValueSafePtr> mem_value_list;
    mem_gdbmi_list->get_value_content (mem_value_list);

    //there should only be one 'row'
    if (mem_value_list.size () != 1) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    std::list<GDBMIValueSafePtr>::const_iterator mem_tuple_iter =
                                                    mem_value_list.begin ();
    if ((*mem_tuple_iter)->content_type ()
        != GDBMIValue::TUPLE_TYPE) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    const GDBMITupleSafePtr gdbmi_tuple =
                            (*mem_tuple_iter)->get_tuple_content ();

    std::list<GDBMIResultSafePtr> result_list;
    result_list = gdbmi_tuple->content ();
    if (result_list.size () < 2) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    std::vector<uint8_t> memory_values;
    bool seen_addr = false, seen_data = false;
    std::list<GDBMIResultSafePtr>::const_iterator result_iter;
    for (result_iter = result_list.begin ();
         result_iter != result_list.end ();
         ++result_iter) {
        if ((*result_iter)->variable () == "addr") {
            seen_addr = true;
            if ((*result_iter)->value ()->content_type ()
                != GDBMIValue::STRING_TYPE) {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            std::istringstream istream
                            ((*result_iter)->value ()-> get_string_content ());
            istream >> std::hex >> a_start_addr;
        } else if ((*result_iter)->variable () == "data") {
            seen_data = true;
            if ((*result_iter)->value ()->content_type ()
                != GDBMIValue::LIST_TYPE) {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            GDBMIListSafePtr gdbmi_list =
                            (*result_iter)->value ()->get_list_content ();
            if (gdbmi_list->content_type () != GDBMIList::VALUE_TYPE) {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            std::list<GDBMIValueSafePtr> gdbmi_values;
            gdbmi_list->get_value_content (gdbmi_values);
            std::list<GDBMIValueSafePtr>::const_iterator val_iter;
            for (val_iter = gdbmi_values.begin ();
                 val_iter != gdbmi_values.end ();
                 ++val_iter) {
                if ((*val_iter)->content_type ()
                    != GDBMIValue::STRING_TYPE) {
                    LOG_PARSING_ERROR (cur);
                    return false;
                }
                std::istringstream istream
                                        ((*val_iter)->get_string_content ());
                //if I use a uint8_t type here, it doesn't seem to work, so
                //using a 16-bit value that will be cast down to 8 bits
                uint16_t byte_val;
                istream >> std::hex >> byte_val;
                memory_values.push_back (byte_val);
            }
        }
        //else ignore it
    }

    if (!seen_addr || !seen_data) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    a_values = memory_values;
    a_to = cur;
    return true;
}

bool
GDBMIParser::parse_asm_instruction_list
                                (UString::size_type a_from,
                                 UString::size_type &a_to,
                                 std::list<common::Asm> &a_instrs)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);
    UString::size_type cur = a_from;

    if (RAW_INPUT.compare (cur,
                           strlen (PREFIX_ASM_INSTRUCTIONS),
                           PREFIX_ASM_INSTRUCTIONS)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    cur += strlen (PREFIX_ASM_INSTRUCTIONS);
    if (RAW_CHAR_AT (cur) != '[') {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    // A GDB/MI LIST of asm instruction descriptors
    //
    // Each insn descriptor is either a TUPLE or a RESULT:
    // When it's a TUPLE, it represents a  pure asm insn
    // that looks like:
    // {address="0x000107bc",func-name="main",offset="0",
    //  inst="save  %sp, -112, %sp"}
    // When it's a RESULT, it represents a mixed source/asm insn that
    // looks like:
    // src_and_asm_line=
    //  {
    //    line="31",file="basics.c",
    //    line_asm_insn=[{address="0x000107bc",func-name="main",offset="0",
    //                    inst="save  %sp, -112, %sp"}]
    //  }
    GDBMIListSafePtr gdbmi_list;

    // OK, now parse the LIST.
    if (!parse_gdbmi_list (cur, cur, gdbmi_list)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    // If gdbmi_list is empty, gdbmi_list->content_type will yield
    // GDBMIList::UNDEFINED_TYPE, so lets test it now and return early
    // if necessary.
    if (gdbmi_list->empty ()) {
        a_to = cur;
        a_instrs.clear ();
        return true;
    }
    // So the content of the list gdbmi_list is either a list of TUPLES,
    // or a list of result, like described earlier. Figure out which is
    // which and parse the damn thing accordingly.
    if (gdbmi_list->content_type () == GDBMIList::VALUE_TYPE) {
        list<common::AsmInstr> instrs;
        if (!analyse_pure_asm_instrs (gdbmi_list, instrs, cur)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        list<common::AsmInstr>::const_iterator it;
        for (it = instrs.begin (); it != instrs.end (); ++it) {
            a_instrs.push_back (*it);
        }
    } else if (gdbmi_list->content_type () == GDBMIList::RESULT_TYPE) {
        list<common::MixedAsmInstr> instrs;
        if (!analyse_mixed_asm_instrs (gdbmi_list, instrs, cur)) {
            LOG_PARSING_ERROR (cur);
            return false;
        }
        list<common::MixedAsmInstr>::const_iterator it;
        for (it = instrs.begin (); it != instrs.end (); ++it) {
            a_instrs.push_back (*it);
        }
    } else {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    a_to = cur;
    return true;
}

// Analyse the list of TUPLEs a_gdbmi_list of the form:
// {
//  address=\"0x00000000004004f3\",
//  func-name=\"main\",
//  offset=\"15\",
//  inst=\"mov    $0x4005fc,%edi\"
//  }
// and extract the list AsmInstr it contains.
// Unknown RESULTs contained in the TUPLEs are ignored, and TUPLEs
// having less than the 4 RESULTs listed above are correctly parsed too.
// E.g.:
// {
//  address=\"0x0000000000400506\",inst=\"nop\"
// }
//
bool
GDBMIParser::analyse_pure_asm_instrs (GDBMIListSafePtr a_gdbmi_list,
                                      list<common::AsmInstr> &a_instrs,
                                      string::size_type /*a_cur*/)
{
    list<GDBMIValueSafePtr> vals;
    a_gdbmi_list->get_value_content (vals);
    list<GDBMIValueSafePtr>::const_iterator val_iter;
    common::AsmInstr asm_instr;
    // Loop over the tuples contained in a_gdbmi_list.
    // Each tuple represents an asm instruction descriptor that can have
    // up to four fields:
    // 1/ the address of the instruction,
    // 2/ name of the function the instruction is located in,
    // 3/ the offset of the instruction inside the function,
    // 4/ a string representing the asm intruction.
    for (val_iter = vals.begin (); val_iter != vals.end (); ++val_iter) {
        if ((*val_iter)->content_type () != GDBMIValue::TUPLE_TYPE) {
            return false;;
        }
        GDBMITupleSafePtr tuple = (*val_iter)->get_tuple_content ();
        THROW_IF_FAIL (tuple);
        std::list<GDBMIResultSafePtr> result_list = tuple->content ();
        LOG_DD ("insn tuple size: " << (int) result_list.size ());

        GDBMIValueSafePtr val;
        GDBMIValue::Type content_type;
        string addr, func_name, instr, offset;
        list<GDBMIResultSafePtr>::const_iterator res_iter;
        for (res_iter = result_list.begin ();
             res_iter != result_list.end ();
             ++res_iter) {
            val = (*res_iter)->value ();
            content_type = val->content_type ();
            if ((*res_iter)->variable () == "address"
                && content_type == GDBMIValue::STRING_TYPE) {
                // get address field
                addr = val->get_string_content ().raw ();
                LOG_DD ("addr: " << addr);
            } else if ((*res_iter)->variable () == "func-name"
                       && content_type == GDBMIValue::STRING_TYPE) {
                // get func-name field
                func_name = val->get_string_content ();
                LOG_DD ("func-name: " << func_name);
            } else if ((*res_iter)->variable () == "offset"
                       && content_type == GDBMIValue::STRING_TYPE) {
                // get offset field
                offset = val->get_string_content ().raw ();
                LOG_DD ("offset: " << offset);
            } else if ((*res_iter)->variable () == "inst"
                       && content_type == GDBMIValue::STRING_TYPE) {
                // get instr field
                instr = val->get_string_content ();
                LOG_DD ("instr: " << instr);
            }
        }
        asm_instr = common::AsmInstr (addr, func_name, offset, instr);
        a_instrs.push_back (asm_instr);
    }
    return true;
}

// Analyse the list of RESULTs of the form in which each RESULT has the
// form:
//
// {
//  src_and_asm_line=
//  {
//    line="31",
//    file="/testsuite/gdb.mi/basics.c",
//    line_asm_insn=[{address="0x000107bc",func-name="main",offset="0",
//                    inst="save  %sp, -112, %sp"}]
//  }
//  Return a list of common::MixedAsmInstr representing the list of
//  RESULT above.
bool
GDBMIParser::analyse_mixed_asm_instrs (GDBMIListSafePtr a_gdbmi_list,
                                       list<common::MixedAsmInstr> &a_instrs,
                                       string::size_type a_cur)
{

    if (a_gdbmi_list->content_type () != GDBMIList::RESULT_TYPE)
        return false;

    list<GDBMIResultSafePtr> outer_results;
    GDBMIValue::Type inner_result_type;
    a_gdbmi_list->get_result_content (outer_results);
    list<GDBMIResultSafePtr>::const_iterator outer_it, inner_it;
    // Loop over the results tuples contained in a_gdbmi_list. There are
    // at least 3 results in the list:
    // 1/ line=<source-line-number>
    // 2/ file=<source-file-path>
    // 3/ line_asm_insn=[<tuples>], where <tuples> are tuples that can
    // be analysed by analyse_pure_asm_instrs.

    for (outer_it = outer_results.begin ();
         outer_it != outer_results.end ();
         ++outer_it) {
        if ((*outer_it)->variable () != "src_and_asm_line") {
                stringstream s;
                s << "got result named " << (*outer_it)->variable ()
                  << " instead of src_and_asm_line";
                LOG_PARSING_ERROR_MSG (a_cur, s.str ());
                return false;
        }
        if ((*outer_it)->value ()->content_type ()
            != GDBMIValue::TUPLE_TYPE) {
            stringstream s;
            s << "got value of type " << (*outer_it)->value ()->content_type ()
              << " instead of TUPLE_TYPE (3)";
            LOG_PARSING_ERROR_MSG (a_cur, s.str ());
            return false;
        }

        const list<GDBMIResultSafePtr> &inner_results =
                (*outer_it)->value ()->get_tuple_content ()->content ();
        common::MixedAsmInstr instr;
        for (inner_it = inner_results.begin ();
             inner_it != inner_results.end ();
             ++inner_it) {
            GDBMIValueSafePtr val = (*inner_it)->value ();
            inner_result_type = val->content_type ();
            if ((*inner_it)->variable () == "line"
                && inner_result_type == GDBMIValue::STRING_TYPE) {
                string line_str = val->get_string_content ().raw ();
                if (!str_utils::string_is_decimal_number (line_str)) {
                    stringstream s;
                    s << "The value of the 'line' RESULT should be "
                         "a number. Instead it was: "
                      << line_str;
                    LOG_PARSING_ERROR_MSG (a_cur, s.str ());
                    return false;
                }
                instr.line_number (atoi (line_str.c_str ()));
            } else if ((*inner_it)->variable () == "file"
                       && inner_result_type == GDBMIValue::STRING_TYPE) {
                instr.file_path (val->get_string_content ());
            } else if ((*inner_it)->variable () == "line_asm_insn"
                       && inner_result_type == GDBMIValue::LIST_TYPE) {
                list<common::AsmInstr> &instrs = instr.instrs ();
                if (!analyse_pure_asm_instrs (val->get_list_content (),
                                              instrs, a_cur)) {
                    stringstream s;
                    s << "Could not parse the instrs of this mixed asm/src "
                         "tuple." ;
                    LOG_PARSING_ERROR_MSG (a_cur, s.str ());
                    return false;
                }
            }
        }
        a_instrs.push_back (instr);
    }
    return true;
}

bool
GDBMIParser::parse_variable (UString::size_type a_from,
                             UString::size_type &a_to,
                             IDebugger::VariableSafePtr &a_var)
{
    LOG_FUNCTION_SCOPE_NORMAL_D (GDBMI_PARSING_DOMAIN);

    UString::size_type cur=a_from;

    if (RAW_INPUT.compare (cur, strlen (PREFIX_NAME), PREFIX_NAME)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    IDebugger::VariableSafePtr var (new IDebugger::Variable);
    // Okay, this is going to be a sequence of RESULT (name=value), where
    // RESULTs are separated by a comma, e.g:
    // name="var1", numchild="0", value="0",type="int"
    GDBMIResultSafePtr result;
    UString value;
    bool got_variable = false;
    SKIP_BLANK (cur);
    while (!END_OF_INPUT (cur)
           && RAW_CHAR_AT (cur) != '\n'
           && parse_gdbmi_result (cur, cur, result)
           && result) {
        LOG_D ("result variable name: " << result->variable (),
               GDBMI_PARSING_DOMAIN);
        // the value of result must be a string
        THROW_IF_FAIL (result->value ()
                       && (result->value ()->content_type ()
                           == GDBMIValue::STRING_TYPE));
        value = result->value ()->get_string_content ();
        if (result->variable () == "name") {
            var->internal_name (value);
            got_variable = true;
        } else if (result->variable () == "value") {
            var->value (value);
        } else if (result->variable () == "numchild") {
            int s = atoi (value.c_str ());
            var->num_expected_children (s);
            LOG_D ("num children: " << s, GDBMI_PARSING_DOMAIN);
        } else if (result->variable () == "type") {
            var->type (value);
        } else if (result->variable () == "displayint") {
            var->display_hint (value);
        } else if (result->variable () == "dynamic") {
            var->is_dynamic ((value == "0" ) ? false: true);
        } else if (result->variable () == "has_more") {
            var->has_more_children ((value == "0") ? false : true);
        } else {
            LOG_D ("hugh? unknown result variable: " << result->variable (),
                   GDBMI_PARSING_DOMAIN);
        }
        SKIP_BLANK (cur);
        if (RAW_CHAR_AT (cur) == ',') {
            cur++;
        }
        SKIP_BLANK (cur);
    }

    if (got_variable) {
        a_to = cur;
        a_var = var;
        return true;
    }
    return false;
}

bool
GDBMIParser::parse_overloads_choice_prompt
                        (UString::size_type a_from,
                         UString::size_type &a_to,
                         vector<IDebugger::OverloadsChoiceEntry> &a_prompts)
{
    UString::size_type cur=a_from;
    gunichar c = 0;

    if (m_priv->index_passed_end (cur)) {
        LOG_PARSING_ERROR (cur);
        return false;
    }

    if (RAW_CHAR_AT (cur) != '[') {
        LOG_PARSING_ERROR (cur);
        return false;
    }
    c = RAW_CHAR_AT (cur);

    string index_str;
    vector<IDebugger::OverloadsChoiceEntry> prompts;
    while (c == '[') {
        ++cur;
        CHECK_END (cur);
        index_str.clear ();
        c = RAW_CHAR_AT (cur);
        //look for the numerical index of the current prompt entry
        for (;;) {
            CHECK_END (cur);
            c = RAW_CHAR_AT (cur);
            if (isdigit (c)) {
                index_str += RAW_CHAR_AT (cur);
            } else if (c == ']') {
                break;
            } else {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            ++cur;
            if (m_priv->index_passed_end (cur))
                break;
            c = RAW_CHAR_AT (cur);
        }
        //we should have the numerical index of the current prompt entry
        //by now.
        if (index_str.empty ()) {
            LOG_ERROR ("Could not parse prompt index");
            return false;
        }
        LOG_DD ("prompt index: " << index_str);
        ++cur;
        CHECK_END (cur);
        SKIP_WS (cur);
        c = RAW_CHAR_AT (cur);

        //now parse the prompt value.
        //it is either  "all", "cancel", or a function name/file location.
        IDebugger::OverloadsChoiceEntry entry;
        entry.index (atoi (index_str.c_str ()));
        if (m_priv->end - cur >= 3
            && !RAW_INPUT.compare (cur, 3, "all")) {
            entry.kind (IDebugger::OverloadsChoiceEntry::ALL);
            cur += 3;
            SKIP_WS (cur);
            c = RAW_CHAR_AT (cur);
            LOG_DD ("pushing entry: " << (int) entry.index ()
                    << ", all" );
            prompts.push_back (entry);
            if (!m_priv->index_passed_end (cur+1)
                && c == '\\' && RAW_CHAR_AT (cur+1) == 'n') {
                cur += 2;
                c = RAW_CHAR_AT (cur);
            }
        } else if  (m_priv->end - cur >= 6
                    && !RAW_INPUT.compare (cur, 6, "cancel")) {
            entry.kind (IDebugger::OverloadsChoiceEntry::CANCEL);
            cur += 6;
            SKIP_WS (cur);
            c = RAW_CHAR_AT (cur);
            LOG_DD ("pushing entry: " << (int) entry.index ()
                    << ", cancel");
            prompts.push_back (entry);
            if (!m_priv->index_passed_end (cur+1)
                && c == '\\' && RAW_CHAR_AT (cur+1) == 'n') {
                cur += 2;
                c = RAW_CHAR_AT (cur);
            }
        } else {
            //try to parse a breakpoint location
            UString function_name, file_name, line_num;
            UString::size_type b=0, e=0;
            b = cur;
            while (!m_priv->index_passed_end (cur)
                   && RAW_INPUT.compare (cur, 4, " at ")) {
                ++cur;
            }
            c = RAW_CHAR_AT (cur);
            if (!m_priv->index_passed_end (cur)
                && !RAW_INPUT.compare (cur, 4, " at ")) {
                e = cur;
            } else {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            function_name.assign (RAW_INPUT, b, e-b);

            cur += 4;
            SKIP_WS (cur);
            c = RAW_CHAR_AT (cur);
            b=cur; e=0;
            while (c != ':') {
                ++cur;
                if (m_priv->index_passed_end (cur))
                    break;
                c = RAW_CHAR_AT (cur);
            }
            if (!m_priv->index_passed_end (cur) && c == ':') {
                e = cur;
            } else {
                LOG_PARSING_ERROR (cur);
                return false;
            }
            file_name.assign (RAW_INPUT, b, e-b);
            ++cur;
            SKIP_WS (cur);
            c = RAW_CHAR_AT (cur);

            while (isdigit (c)) {
                line_num += c;
                ++cur;
                if (m_priv->index_passed_end (cur))
                    break;
                c = RAW_CHAR_AT (cur);
            }
            entry.kind (IDebugger::OverloadsChoiceEntry::LOCATION);
            entry.function_name (function_name);
            entry.file_name (file_name);
            entry.line_number (atoi (line_num.c_str ()));
            LOG_DD ("pushing entry: " << (int) entry.index ()
                    << ", " << entry.function_name ());
            prompts.push_back (entry);
            if (m_priv->index_passed_end (cur)) {
                LOG_DD ("reached end, getting out");
                break;
            }
            SKIP_WS (cur);
            c = RAW_CHAR_AT (cur);
            if (!m_priv->index_passed_end (cur+1)
                && c == '\\' && RAW_CHAR_AT (cur+1) == 'n') {
                cur += 2;
                c = RAW_CHAR_AT (cur);
            }
            SKIP_WS (cur);
            c = RAW_CHAR_AT (cur);
            if (m_priv->index_passed_end (cur))
                break;
        }
    }//end while//parse a prompt entry
    if (prompts.empty ())
        return false;
    a_prompts = prompts;
    a_to = cur;
    return true;
}

//******************************
//</Parser methods>
//******************************
NEMIVER_END_NAMESPACE (nemiver)

