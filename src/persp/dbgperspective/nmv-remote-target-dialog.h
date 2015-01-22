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
#ifndef __NMV_REMOTE_TARGET_DIALOG_H__
#define __NMV_REMOTE_TARGET_DIALOG_H__
#include "nmv-dialog.h"

NEMIVER_BEGIN_NAMESPACE (nemiver)

class RemoteTargetDialog : public Dialog {
    struct Priv;
    SafePtr<Priv> m_priv;

public:

    enum ConnectionType {
        TCP_CONNECTION_TYPE,
        SERIAL_CONNECTION_TYPE,LINE_COMMAND_TYPE
    };

    RemoteTargetDialog (const UString &a_root_path);
    virtual ~RemoteTargetDialog ();

    const UString& get_cwd () const;
    void set_cwd (const UString &);

    const UString& get_executable_path () const;
    void set_executable_path (const UString &a_path);

    const UString& get_solib_prefix_path () const;
    void set_solib_prefix_path (const UString &a_path);

    ConnectionType get_connection_type ();
    void set_connection_type (ConnectionType a_type);

    const UString& get_server_address () const;
    void set_server_address (const UString &a_address);

    unsigned get_server_port () const;
    void set_server_port (unsigned a_port);

    const UString& get_serial_port_name () const;
    void set_serial_port_name (const UString &a_serial);


};//end RemoteTargetDialog

NEMIVER_END_NAMESPACE (nemiver)

#endif //__NMV_REMOTE_TARGET_DIALOG_H__
