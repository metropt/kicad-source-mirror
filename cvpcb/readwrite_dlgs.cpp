/**
 * @file cvpcb/readwrite_dlgs.cpp
 */

/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2012 Jean-Pierre Charras, jean-pierre.charras
 * Copyright (C) 2011 Wayne Stambaugh <stambaughw@verizon.net>
 * Copyright (C) 1992-2011 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <fctsys.h>
#include <common.h>
#include <confirm.h>
#include <build_version.h>
#include <macros.h>
#include <fpid.h>
#include <fp_lib_table.h>
#include <reporter.h>
#include <html_messagebox.h>

#include <cvpcb.h>
#include <cvpcb_mainframe.h>
#include <cvstruct.h>
#include <wildcards_and_files_ext.h>

#define titleComponentLibErr _( "Component Library Error" )

void CVPCB_MAINFRAME::SetNewPkg( const wxString& aFootprintName )
{
    COMPONENT* component;
    bool       hasFootprint = false;
    int        componentIndex;
    wxString   description;

    if( m_netlist.IsEmpty() )
        return;

    // If no component is selected, select the first one
    if( m_ListCmp->GetFirstSelected() < 0 )
    {
        componentIndex = 0;
        m_ListCmp->SetSelection( componentIndex, true );
    }

    // iterate over the selection
    while( m_ListCmp->GetFirstSelected() != -1 )
    {
        // Get the component for the current iteration
        componentIndex = m_ListCmp->GetFirstSelected();
        component = m_netlist.GetComponent( componentIndex );

        if( component == NULL )
            return;

        // Check to see if the component has already a footprint set.
        hasFootprint = !component->GetFPID().empty();

        FPID fpid;

        if( !aFootprintName.IsEmpty() )
        {
            wxCHECK_RET( fpid.Parse( aFootprintName ) < 0,
                         wxString::Format( wxT( "<%s> is not a valid FPID." ),
                                           GetChars( aFootprintName ) ) );
        }

        component->SetFPID( fpid );

        // create the new component description
        description.Printf( CMP_FORMAT, componentIndex + 1,
                            GetChars( component->GetReference() ),
                            GetChars( component->GetValue() ),
                            GetChars( FROM_UTF8( component->GetFPID().Format().c_str() ) ) );

        // If the component hasn't had a footprint associated with it
        // it now has, so we decrement the count of components without
        // a footprint assigned.
        if( !hasFootprint )
        {
            hasFootprint = true;
            m_undefinedComponentCnt -= 1;
        }

        // Set the new description and deselect the processed component
        m_ListCmp->SetString( componentIndex, description );
        m_ListCmp->SetSelection( componentIndex, false );
    }

    // Mark this "session" as modified
    m_modified = true;

    // select the next component, if there is one
    if( componentIndex < (m_ListCmp->GetCount() - 1) )
        componentIndex++;

    m_ListCmp->SetSelection( componentIndex, true );

    // update the statusbar
    DisplayStatus();
}


#if 0

    /*

    This code block was based on two major assumptions that are no longer true:
    1) Footprint library basenames would remain the same.
    (But no, basenames have been renamed in the github repo.)
    2) *.mod files would still be around and merely reside in the FP_LIB_TABLE.
    (But no, they have been converted to *.pretty.)

    There is a newer replacement code block in the #else region.

    */

/**
 * Function missingLegacyLibs
 * tests the list of \a aLibNames by URI to determine if any of them are missing from
 * the #FP_LIB_TABLE.
 *
 * @note The missing legacy footprint library test is performed by using old library
 *       file path lookup method.  If the library is found, it is compared against all
 *       of the URIs in the table rather than the nickname.  This was done because the
 *       user could change the nicknames from the default table.  Using the full path
 *       is more reliable.
 *
 * @param aLibNames is the list of legacy library names.
 * @param aErrorMsg is a pointer to a wxString object to store the URIs of any missing
 *                  legacy library paths.  Can be NULL.
 * @return true if there are missing legacy libraries.  Otherwise false.
 */
static bool missingLegacyLibs( FP_LIB_TABLE* aTbl, SEARCH_STACK& aSStack,
        const wxArrayString& aLibNames, wxString* aErrorMsg )
{
    bool missing = false;

    for( unsigned i = 0;  i < aLibNames.GetCount();  i++ )
    {
        wxFileName  fn( wxEmptyString, aLibNames[i], LegacyFootprintLibPathExtension );

        wxString    legacyLibPath = aSStack.FindValidPath( fn.GetFullPath() );

        /*
        if( legacyLibPath.IsEmpty() )
            continue;
        */

        if( !aTbl->FindRowByURI( legacyLibPath ) )
        {
            missing = true;

            if( aErrorMsg )
            {
                *aErrorMsg += wxChar( '"' );

                if( !legacyLibPath )
                    *aErrorMsg += !legacyLibPath ? aLibNames[i] : legacyLibPath;

                *aErrorMsg += wxT( "\"\n" );
            }
        }
    }

    return missing;
}


/**
 * Function convertFromLegacy
 * converts the footprint names in \a aNetList from the legacy format to the #FPID format.
 *
 * @param aNetList is the #NETLIST object to convert.
 * @param aLibNames is the list of legacy footprint library names from the currently loaded
 *                  project.
 * @param aReporter is the #REPORTER object to dump messages into.
 * @return true if all footprint names were successfully converted to a valid FPID.
 */
static bool convertFromLegacy( FP_LIB_TABLE* aTbl, SEARCH_STACK& aSStack, NETLIST& aNetList,
        const wxArrayString& aLibNames, REPORTER* aReporter = NULL ) throw( IO_ERROR )
{
    wxString   msg;
    FPID       lastFPID;
    COMPONENT* component;
    MODULE*    module = 0;
    bool       retv = true;

    if( aNetList.IsEmpty() )
        return true;

    aNetList.SortByFPID();

    wxString   libPath;

    PLUGIN::RELEASER pi( IO_MGR::PluginFind( IO_MGR::LEGACY ) );

    for( unsigned ii = 0; ii < aNetList.GetCount(); ii++ )
    {
        component = aNetList.GetComponent( ii );

        // The footprint hasn't been assigned yet so ignore it.
        if( component->GetFPID().empty() )
            continue;

        if( component->GetFPID() != lastFPID )
        {
            module = NULL;

            for( unsigned ii = 0; ii < aLibNames.GetCount(); ii++ )
            {
                wxFileName fn( wxEmptyString, aLibNames[ii], LegacyFootprintLibPathExtension );

                libPath = aSStack.FindValidPath( fn.GetFullPath() );

                if( !libPath )
                {
                    if( aReporter )
                    {
                        msg.Printf( _( "Cannot find footprint library file '%s' in any of the "
                                       "KiCad legacy library search paths.\n" ),
                                    GetChars( fn.GetFullPath() ) );
                        aReporter->Report( msg );
                    }

                    retv = false;
                    continue;
                }

                module = pi->FootprintLoad( libPath, component->GetFPID().GetFootprintName() );

                if( module )
                {
                    lastFPID = component->GetFPID();
                    break;
                }
            }
        }

        if( !module )
        {
            if( aReporter )
            {
                msg.Printf( _( "Component '%s' footprint '%s' was not found in any legacy "
                               "library.\n" ),
                            GetChars( component->GetReference() ),
                            GetChars( component->GetFPID().Format() ) );
                aReporter->Report( msg );
            }

            // Clear the footprint assignment since the old library lookup method is no
            // longer valid.
            FPID emptyFPID;

            component->SetFPID( emptyFPID );
            retv = false;
            continue;
        }
        else
        {
            wxString    libNickname;

            const FP_LIB_TABLE::ROW* row;

            if( ( row = aTbl->FindRowByURI( libPath ) ) != NULL )
                libNickname = row->GetNickName();

            if( libNickname.IsEmpty() )
            {
                if( aReporter )
                {
                    msg.Printf( _( "Component '%s' with footprint '%s' and legacy library path '%s' "
                                   "was not found in the footprint library table.\n" ),
                                GetChars( component->GetReference() ),
                                GetChars( component->GetFPID().Format() ),
                                GetChars( libPath )
                                );
                    aReporter->Report( msg );
                }

                retv = false;
            }
            else
            {
                FPID newFPID = lastFPID;
                newFPID.SetLibNickname( libNickname );

                if( !newFPID.IsValid() )
                {
                    if( aReporter )
                    {
                        msg.Printf( _( "Component '%s' FPID '%s' is not valid.\n" ),
                                    GetChars( component->GetReference() ),
                                    GetChars( newFPID.Format() ) );
                        aReporter->Report( msg );
                    }

                    retv = false;
                }
                else
                {
                    // The footprint name should already be set.
                    component->SetFPID( newFPID );
                }
            }
        }
    }

    return retv;
}


bool CVPCB_MAINFRAME::ReadNetListAndLinkFiles()
{
    COMPONENT* component;
    wxString   msg;
    bool       isLegacy = true;

    ReadSchematicNetlist();

    if( m_ListCmp == NULL )
        return false;

    LoadProjectFile( m_NetlistFileName.GetFullPath() );
    LoadFootprintFiles();
    BuildFOOTPRINTS_LISTBOX();
    BuildLIBRARY_LISTBOX();

    m_ListCmp->Clear();
    m_undefinedComponentCnt = 0;

    if( m_netlist.AnyFootprintsLinked() )
    {
        for( unsigned i = 0;  i < m_netlist.GetCount();  i++ )
        {
            component = m_netlist.GetComponent( i );

            if( component->GetFPID().empty() )
                continue;

            if( isLegacy )
            {
                if( !component->GetFPID().IsLegacy() )
                    isLegacy = false;
            }
        }
    }
    else
    {
        isLegacy = false;  // None of the components have footprints assigned.
    }

    wxString missingLibs;

    // Check if footprint links were generated before the footprint library table was implemented.
    if( isLegacy )
    {
        if( missingLegacyLibs( FootprintLibs(), Prj().PcbSearchS(), m_ModuleLibNames, &missingLibs ) )
        {
            msg = wxT( "The following legacy libraries are defined in the project file "
                       "but were not found in the footprint library table:\n\n" ) + missingLibs;
            msg += wxT( "\nDo you want to update the footprint library table before "
                        "attempting to update the assigned footprints?" );

            if( IsOK( this, msg ) )
            {
                wxCommandEvent cmd;

                OnEditFootprintLibraryTable( cmd );
            }
        }

        msg = wxT( "Some or all of the assigned footprints contain legacy entries.  Would you "
                   "like CvPcb to attempt to convert them to the new footprint library table "
                   "format?" );

        if( IsOK( this, msg ) )
        {
            msg.Clear();
            WX_STRING_REPORTER reporter( &msg );

            SEARCH_STACK&   search = Prj().SchSearchS();

            if( !convertFromLegacy( FootprintLibs(), search, m_netlist, m_ModuleLibNames, &reporter ) )
            {
                HTML_MESSAGE_BOX dlg( this, wxEmptyString );

                dlg.MessageSet( wxT( "The following errors occurred attempting to convert the "
                                     "footprint assignments:\n\n" ) );
                dlg.ListSet( msg );
                dlg.MessageSet( wxT( "\nYou will need to reassign them manually if you want them "
                                     "to be updated correctly the next time you import the "
                                     "netlist in Pcbnew." ) );
                dlg.ShowModal();
            }

            m_modified = true;
        }
        else
        {
            // Clear the legacy footprint assignments.
            for( unsigned i = 0;  i < m_netlist.GetCount();  i++ )
            {
                FPID emptyFPID;
                component = m_netlist.GetComponent( i );
                component->SetFPID( emptyFPID );
                m_modified = true;
            }
        }
    }

    for( unsigned i = 0;  i < m_netlist.GetCount();  i++ )
    {
        component = m_netlist.GetComponent( i );

        msg.Printf( CMP_FORMAT, m_ListCmp->GetCount() + 1,
                    GetChars( component->GetReference() ),
                    GetChars( component->GetValue() ),
                    GetChars( FROM_UTF8( component->GetFPID().Format().c_str() ) ) );

        m_ListCmp->AppendLine( msg );

        if( component->GetFPID().empty() )
        {
            m_undefinedComponentCnt += 1;
            continue;
        }
    }

    if( !m_netlist.IsEmpty() )
        m_ListCmp->SetSelection( 0, true );

    DisplayStatus();

    UpdateTitle();

    UpdateFileHistory( m_NetlistFileName.GetFullPath() );

    return true;
}

#else   // new strategy

/// Return true if the resultant FPID has a certain nickname.  The guess
/// is only made if this footprint resides in only one library.
/// @return int - 0 on success, 1 on not found, 2 on ambiguous i.e. multiple matches
static int guessNickname( FP_LIB_TABLE* aTbl, FPID* aFootprintId )
{
    if( aFootprintId->GetLibNickname().size() )
        return 0;

    wxString    nick;
    wxString    fpname = aFootprintId->GetFootprintName();

    std::vector<wxString> nicks = aTbl->GetLogicalLibs();

    // Search each library going through libraries alphabetically.
    for( unsigned libNdx = 0;  libNdx<nicks.size();  ++libNdx )
    {
        wxArrayString fpnames = aTbl->FootprintEnumerate( nicks[libNdx] );

        for( unsigned nameNdx = 0;  nameNdx<fpnames.size();   ++nameNdx )
        {
            if( fpname == fpnames[nameNdx] )
            {
                if( !nick )
                    nick = nicks[libNdx];
                else
                    return 2;       // duplicate, the guess would not be certain
            }
        }
    }

    if( nick.size() )
    {
        aFootprintId->SetLibNickname( nick );
        return 0;
    }

    return 1;
}


bool CVPCB_MAINFRAME::ReadNetListAndLinkFiles()
{
    wxString        msg;
    bool            hasMissingNicks = false;
    FP_LIB_TABLE*   tbl = FootprintLibs();

    ReadSchematicNetlist();

    if( m_ListCmp == NULL )
        return false;

    LoadProjectFile( m_NetlistFileName.GetFullPath() );
    LoadFootprintFiles();
    BuildFOOTPRINTS_LISTBOX();
    BuildLIBRARY_LISTBOX();

    m_ListCmp->Clear();
    m_undefinedComponentCnt = 0;

    if( m_netlist.AnyFootprintsLinked() )
    {
        for( unsigned i = 0;  i < m_netlist.GetCount();  i++ )
        {
            COMPONENT* component = m_netlist.GetComponent( i );

            if( component->GetFPID().empty() )
                continue;

            if( component->GetFPID().IsLegacy() )
                hasMissingNicks = true;
        }
    }

    // Check if footprint links were generated before the footprint library table was implemented.
    if( hasMissingNicks )
    {
        msg = wxT(
            "Some of the assigned footprints are legacy entries (are missing lib nicknames). "
            "Would you like CvPcb to attempt to convert them to the new required FPID format? "
            "(If you answer no, then these assignments will be cleared out and you will "
            "have to re-assign these footprints yourself.)"
            );

        if( IsOK( this, msg ) )
        {
            msg.Clear();

            try
            {
                for( unsigned i = 0;  i < m_netlist.GetCount();  i++ )
                {
                    COMPONENT* component = m_netlist.GetComponent( i );

                    if( component->GetFPID().IsLegacy() )
                    {
                        int guess = guessNickname( tbl, (FPID*) &component->GetFPID() );

                        switch( guess )
                        {
                        case 0:
                            DBG(printf("%s: guessed OK ref:%s  fpid:%s\n", __func__,
                                TO_UTF8( component->GetReference() ), component->GetFPID().Format().c_str() );)
                            m_modified = true;
                            break;

                        case 1:
                            msg += wxString::Format( _(
                                    "Component '%s' footprint '%s' was <b>not found</b> in any library.\n" ),
                                    GetChars( component->GetReference() ),
                                    GetChars( component->GetFPID().GetFootprintName() )
                                    );
                            break;

                        case 2:
                            msg += wxString::Format( _(
                                    "Component '%s' footprint '%s' was found in <b>multiple</b> libraries.\n" ),
                                    GetChars( component->GetReference() ),
                                    GetChars( component->GetFPID().GetFootprintName() )
                                    );
                            break;
                        }
                    }
                }
            }
            catch( const IO_ERROR& ioe )
            {
                wxString msg = ioe.errorText;
                msg += wxT( "\n\n" );
                msg += _( "First check your fp-lib-table entries." );

                wxMessageBox( msg, wxT( "Problematic fp-lib-tables" ) );
                return false;
            }

            if( msg.size() )
            {
                HTML_MESSAGE_BOX dlg( this, wxEmptyString );

                dlg.MessageSet( wxT( "The following errors occurred attempting to convert the "
                                     "footprint assignments:\n\n" ) );
                dlg.ListSet( msg );
                dlg.MessageSet( wxT( "\nYou will need to reassign them manually if you want them "
                                     "to be updated correctly the next time you import the "
                                     "netlist in Pcbnew." ) );

#if 1
                dlg.ShowModal();
#else
                dlg.Fit();
                dlg.Show( true );   // modeless lets user watch while fixing the problems, but its not working.
#endif
            }
        }
        else
        {
            // Clear the legacy footprint assignments.
            for( unsigned i = 0;  i < m_netlist.GetCount();  i++ )
            {
                COMPONENT* component = m_netlist.GetComponent( i );

                if( component->GetFPID().IsLegacy() )
                {
                    component->SetFPID( FPID() /* empty */ );
                    m_modified = true;
                }
            }
        }
    }

    for( unsigned i = 0;  i < m_netlist.GetCount();  i++ )
    {
        COMPONENT* component = m_netlist.GetComponent( i );

        msg.Printf( CMP_FORMAT, m_ListCmp->GetCount() + 1,
                    GetChars( component->GetReference() ),
                    GetChars( component->GetValue() ),
                    GetChars( FROM_UTF8( component->GetFPID().Format().c_str() ) ) );

        m_ListCmp->AppendLine( msg );

        if( component->GetFPID().empty() )
        {
            m_undefinedComponentCnt += 1;
            continue;
        }
    }

    if( !m_netlist.IsEmpty() )
        m_ListCmp->SetSelection( 0, true );

    DisplayStatus();

    UpdateTitle();

    UpdateFileHistory( m_NetlistFileName.GetFullPath() );

    return true;
}


#endif


int CVPCB_MAINFRAME::SaveCmpLinkFile( const wxString& aFullFileName )
{
    wxFileName fn;

    if( !aFullFileName.IsEmpty() )
    {
        fn = m_NetlistFileName;
        fn.SetExt( ComponentFileExtension );
    }
    else
    {
        wxFileDialog dlg( this, _( "Save Component Footprint Link File" ), wxEmptyString,
                          _( "Unnamed file" ), ComponentFileWildcard, wxFD_SAVE );

        if( dlg.ShowModal() == wxID_CANCEL )
            return -1;

        fn = dlg.GetPath();

        if( !fn.HasExt() )
            fn.SetExt( ComponentFileExtension );

        // Save the project specific footprint library table.
        if( !FootprintLibs()->IsEmpty( false ) )
        {
            wxString fp_lib_tbl = Prj().FootprintLibTblName();

            if( wxFileName::FileExists( fp_lib_tbl )
              && IsOK( this, _( "A footprint library table already exists in this path.\n\nDo "
                                "you want to overwrite it?" ) ) )
            {
                try
                {
                    FootprintLibs()->Save( fp_lib_tbl );
                }
                catch( const IO_ERROR& ioe )
                {
                    wxString msg = wxString::Format( _(
                        "An error occurred attempting to save the "
                        "footprint library table '%s'\n\n%s" ),
                        GetChars( fp_lib_tbl ),
                        GetChars( ioe.errorText )
                        );
                    DisplayError( this, msg );
                }
            }
        }
    }

    if( !IsWritable( fn.GetFullPath() ) )
        return 0;

    if( WriteComponentLinkFile( fn.GetFullPath() ) == 0 )
    {
        DisplayError( this, _( "Unable to create component footprint link file (.cmp)" ) );
        return 0;
    }

    wxString msg;
    msg.Printf( _("File %s saved"), GetChars( fn.GetFullPath() ) );
    SetStatusText( msg );
    return 1;
}
