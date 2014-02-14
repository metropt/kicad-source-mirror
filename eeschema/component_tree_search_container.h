/* -*- c++ -*-
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2014 Henner Zeller <h.zeller@acm.org>
 * Copyright (C) 2014 KiCad Developers, see change_log.txt for contributors.
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
 * http: *www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http: *www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include <vector>
#include <wx/string.h>

class LIB_COMPONENT;
class CMP_LIBRARY;
class wxTreeCtrl;
class wxArrayString;

// class COMPONENT_TREE_SEARCH_CONTAINER
// A container for components that allows to search them matching their name, keywords
// and descripotions, updating a wxTreeCtrl with the results (toplevel nodes:
// libraries, leafs: components), scored by relevance.
//
// The scored result list is adpated on each update on the search-term: this allows
// to have a search-as-you-type experience.
class COMPONENT_TREE_SEARCH_CONTAINER
{
public:
    COMPONENT_TREE_SEARCH_CONTAINER();
    ~COMPONENT_TREE_SEARCH_CONTAINER();

    /** Function AddLibrary
     * Add the components of this library to be searched.
     * To be called in the setup phase to fill this container.
     *
     * @param aLib containting all the components to be added.
     */
    void AddLibrary( CMP_LIBRARY& aLib );

    /** Function AddComponentList
     * Add the given list of components, given by name, to be searched.
     * To be called in the setup phase to fill this container.
     *
     * @param aNodeName          The parent node name the components will show up as leaf.
     * @param aComponentNameList List of component names.
     * @param aOptionalLib       Library to look up the component names (if NULL: global lookup)
     * @param aNormallyExpanded  Should the node in the tree be expanded by default.
     */
    void AddComponentList( const wxString& aNodeName, const wxArrayString& aComponentNameList,
                           CMP_LIBRARY* aOptionalLib, bool aNormallyExpanded );

    /** Function SetPreselectNode
     * Set the component name to be selected in absence of any search-result.
     *
     * @param aComponentName the component name to be selected.
     */
    void SetPreselectNode( const wxString& aComponentName );

    /** Function SetTree
     * Set the tree to be manipulated.
     * Each update of the search term will update the tree, with the most
     * scoring component at the top and selected. If a preselect node is set, this
     * is displayed. Does not take ownership of the tree.
     *
     * @param aTree that is to be modified on search updates.
     */
    void SetTree( wxTreeCtrl* aTree );

    /** Function UpdateSearchTerm
     * Update the search string provided by the user and narrow down the result list.
     *
     * This string is a space-separated list of terms, each of which
     * is applied to the components list to narrow it down. Results are scored by
     * relevancy (e.g. exact match scores higher than prefix-match which in turn scores
     * higher than substring match). This updates the search and tree on each call.
     *
     * @param aSearch is the user-provided search string.
     */
    void UpdateSearchTerm( const wxString& aSearch );

    /** Function GetSelectedComponent
     *
     * @return the selected component or NULL if there is none.
     */
    LIB_COMPONENT* GetSelectedComponent();

private:
    struct TREE_NODE;
    static bool scoreComparator( const TREE_NODE* a1, const TREE_NODE* a2 );

    std::vector<TREE_NODE*> nodes;
    wxString preselect_node_name;
    wxTreeCtrl* tree;
};