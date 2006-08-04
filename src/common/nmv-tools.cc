/* -*- Mode: C++; indent-tabs-mode:nil; c-basic-offset:4 -*- */

/*
 *This file is part of the Nemiver Project.
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
#include <fstream>
#include "nmv-tools.h"
#include "nmv-parsing-utils.h"
#include "nmv-sql-statement.h"
#include "nmv-buffer.h"

namespace nemiver {
namespace common {
namespace tools {

bool
execute_sql_command_file (const UString &a_sql_command_file,
                          Transaction &a_trans,
                          ostream &a_ostream)
{
    ifstream inputfile ;
    try {
        inputfile.open (a_sql_command_file.c_str ()) ;
    } catch (exception &e) {
        a_ostream << "could not open file: '"
                  << a_sql_command_file
                  << a_sql_command_file ;
        return false ;
    }

    if (inputfile.bad ()) {
        a_ostream << "could not open file: '"
                  << a_sql_command_file
                  << a_sql_command_file ;
        return false ;
    }

    bool is_ok = execute_sql_commands_from_istream (inputfile,
                                                    a_trans,
                                                    a_ostream);
    inputfile.close () ;
    return is_ok ;
}

bool
execute_sql_commands_from_istream (istream &a_istream,
                                   Transaction &a_trans,
                                   ostream &a_ostream)
{
    //loop parsing everything untill ';' or eof and execute it.
    bool is_ok (false) ;
    UString cmd_line, tmp_str ;
    char c=0;

    TransactionAutoHelper safe_trans (a_trans) ;

    while (true) {
        a_istream.get (c) ;
        if (a_istream.bad ()) {
            return false ;
        }
        if (a_istream.eof ()) {
            tmp_str="" ;
            if (cmd_line != ""
                    && !parsing_utils::is_white_string (cmd_line)) {
                LOG_VERBOSE ("executing: " << cmd_line << "...") ;
                is_ok = execute_one_statement (cmd_line,
                                               a_trans,
                                               a_ostream) ;
                LOG_VERBOSE ("done.") ;
                break ;
            } else {
                break ;
            }
        }
        cmd_line += c ;
        if (c == ';') {
            tmp_str="" ;
            if (cmd_line != ""
                && !parsing_utils::is_white_string (cmd_line)) {
                LOG_VERBOSE ("executing: " << cmd_line << "...") ;
                is_ok = execute_one_statement (cmd_line,
                                               a_trans,
                                               a_ostream) ;
                if (!is_ok) {
                    LOG_VERBOSE ("execution failed") ;
                    return false ;
                }
                LOG_VERBOSE ("done.") ;
            }
            if (!is_ok) {
                return false ;
            }
            cmd_line = "" ;
        }//end (c == ';')
    }

    if (!is_ok) {
        return false ;
    }
    safe_trans.end () ;
    return true ;
}

bool
execute_one_statement (const UString &a_sql_string,
                       Transaction &a_trans,
                       ostream &a_ostream)
{
    bool is_ok = false ;

    TransactionAutoHelper safe_trans (a_trans) ;

    try {
        is_ok = a_trans.get_connection ().execute_statement
                (SQLStatement (a_sql_string)) ;
    } catch (Exception &e) {
        a_ostream << "statement execution error: " << e.what () ;
        LOG_VERBOSE ("error occured when executing statetement: " << a_sql_string) ;
        return false ;
    }
    if (!is_ok) {
        a_ostream << "statement execution failed: "
        << a_trans.get_connection ().get_last_error ()
        << "\n" ;
        LOG_ERROR ("error occured when executing statetement: " <<a_sql_string);
        return false;
    }
    Buffer buffer1, buffer2;
    long number_of_columns = 0 ;
    while (a_trans.get_connection ().read_next_row ()) {
        number_of_columns = a_trans.get_connection ().get_number_of_columns () ;
        a_ostream << "--------------------------------------\n" ;
        for (long i=0 ; i < number_of_columns; ++i) {
            if (!a_trans.get_connection ().get_column_name (i, buffer1)) {
                a_ostream << "error while getting name of column "
                << i
                << " : "
                << a_trans.get_connection ().get_last_error ()
                << "\n" ;
                continue ;
            }
            if (!a_trans.get_connection ().get_column_content (i, buffer2)) {
                a_ostream << "error while getting content of column "
                << i
                << " : "
                << a_trans.get_connection ().get_last_error ()
                << "\n" ;
                continue ;
            }
            a_ostream.write (buffer1.get_data (),
                             buffer1.get_len ()) ;
            a_ostream << " : " ;
            a_ostream.write (buffer2.get_data (),
                             buffer2.get_len ()) ;
            a_ostream << '\n' ;
        }
        a_ostream << "--------------------------------------\n" ;
    }
    safe_trans.end () ;
    return is_ok ;
}

}//end namespace tools
}//end namespace common
}//end namespace nemiver