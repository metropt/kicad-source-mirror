/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright (C) 2009-2013  Lorenzo Mercantonio
 * Copyright (C) 2014  Cirilo Bernado
 * Copyright (C) 2013 Jean-Pierre Charras jp.charras at wanadoo.fr
 * Copyright (C) 2004-2013 KiCad Developers, see change_log.txt for contributors.
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

/*
 * NOTE:
 * 1. for improved looks, create a DRILL layer for PTH drills.
 *      To render the improved board, render the vertical outline only
 *      for the board (no added drill holes), then render the
 *      outline only for PTH, and finally render the top and bottom
 *      of the board. NOTE: if we don't want extra eye-candy then
 *      we must maintain the current board export.
 *      Additional bits needed for improved eyecandy:
 *      + CalcOutline: calculates only the outline of a VRML_LAYER or
 *          a VERTICAL_HOLES
 *      + WriteVerticalIndices: writes the indices of only the vertical
 *          facets of a VRML_LAYER or a VRML_HOLES.
 *      + WriteVerticalVertices: writes only the outline vertices to
 *          form vertical walls; applies to VRML_LAYER and VRML_HOLES
 *
 * 2. How can we suppress fiducials such as those in the corners of the pic-programmer demo?
 *
 * 3. Export Graphics to Layer objects (see 3d_draw.cpp for clues) to ensure that custom
 *      tracks/fills/logos are rendered.
 *  module->TransformGraphicShapesWithClearanceToPolygonSet
 *
 * For mechanical correctness, we should use the following settings with arcs:
 * 1. max. deviation:  the number of edges should be determined by the max.
 *      mechanical deviation and the minimum number of edges shall be 6.
 * 2. for very large features we may introduce too many edges in a circle;
 *      to control this, we should specify a MAX number of edges or a threshold
 *      radius and a deviation for larger features
 *
 * For example, many mechanical fits are to within +/-0.05mm, so specifying
 *      a max. deviation of 0.02mm will yield a hole near the max. material
 *      condition. Calculating sides for a 10mm radius hole will yield about
 *      312 points; such large holes (and arcs) will typically have a specified
 *      tolerance of +/-0.2mm in which case we can set the MAX edges to 32
 *      provided none of the important holes requires > 32 edges.
 *
 */

#include <fctsys.h>
#include <kicad_string.h>
#include <wxPcbStruct.h>
#include <drawtxt.h>
#include <trigo.h>
#include <pgm_base.h>
#include <3d_struct.h>
#include <macros.h>

#include <pcbnew.h>

#include <class_board.h>
#include <class_module.h>
#include <class_track.h>
#include <class_zone.h>
#include <class_edge_mod.h>
#include <class_pcb_text.h>
#include <convert_from_iu.h>

#include "../3d-viewer/modelparsers.h"

#include <vector>
#include <cmath>
#include <vrml_board.h>

/* helper function:
 * some characters cannot be used in names,
 * this function change them to "_"
 */
static void ChangeIllegalCharacters( wxString& aFileName, bool aDirSepIsIllegal );

struct VRML_COLOR
{
    float diffuse_red;
    float diffuse_grn;
    float diffuse_blu;

    float spec_red;
    float spec_grn;
    float spec_blu;

    float emit_red;
    float emit_grn;
    float emit_blu;

    float ambient;
    float transp;
    float shiny;

    VRML_COLOR()
    {
        // default green
        diffuse_red = 0.13;
        diffuse_grn = 0.81;
        diffuse_blu = 0.22;
        spec_red = 0.13;
        spec_grn = 0.81;
        spec_blu = 0.22;
        emit_red = 0.0;
        emit_grn = 0.0;
        emit_blu = 0.0;

        ambient = 1.0;
        transp  = 0;
        shiny   = 0.2;
    }

    VRML_COLOR( float dr, float dg, float db,
                float sr, float sg, float sb,
                float er, float eg, float eb,
                float am, float tr, float sh )
    {
        diffuse_red = dr;
        diffuse_grn = dg;
        diffuse_blu = db;
        spec_red = sr;
        spec_grn = sg;
        spec_blu = sb;
        emit_red = er;
        emit_grn = eg;
        emit_blu = eb;

        ambient = am;
        transp  = tr;
        shiny   = sh;
    }
};

enum VRML_COLOR_INDEX
{
    VRML_COLOR_PCB = 0,
    VRML_COLOR_TRACK,
    VRML_COLOR_SILK,
    VRML_COLOR_TIN,
    VRML_COLOR_LAST
};


class MODEL_VRML
{
private:

    double layer_z[NB_LAYERS];
    VRML_COLOR colors[VRML_COLOR_LAST];

public:

    VRML_LAYER  holes;
    VRML_LAYER  board;
    VRML_LAYER  top_copper;
    VRML_LAYER  bot_copper;
    VRML_LAYER  top_silk;
    VRML_LAYER  bot_silk;
    VRML_LAYER  top_tin;
    VRML_LAYER  bot_tin;

    double scale;           // board internal units to output scaling

    double  tx;             // global translation along X
    double  ty;             // global translation along Y

    double board_thickness; // depth of the PCB

    LAYER_NUM s_text_layer;
    int s_text_width;

    MODEL_VRML()
    {
        for( int i = 0; i < NB_LAYERS; ++i )
            layer_z[i] = 0;

        // this default only makes sense if the output is in mm
        board_thickness = 1.6;

        // pcb green
        colors[ VRML_COLOR_PCB ]    = VRML_COLOR( .07, .3, .12, .07, .3, .12,
                                                  0, 0, 0, 1, 0, 0.2 );
        // track green
        colors[ VRML_COLOR_TRACK ]  = VRML_COLOR( .08, .5, .1, .08, .5, .1,
                                                  0, 0, 0, 1, 0, 0.2 );
        // silkscreen white
        colors[ VRML_COLOR_SILK ]   = VRML_COLOR( .9, .9, .9, .9, .9, .9,
                                                  0, 0, 0, 1, 0, 0.2 );
        // pad silver
        colors[ VRML_COLOR_TIN ] = VRML_COLOR( .749, .756, .761, .749, .756, .761,
                                                  0, 0, 0, 0.8, 0, 0.8 );
    }

    VRML_COLOR& GetColor( VRML_COLOR_INDEX aIndex )
    {
        return colors[aIndex];
    }

    void SetOffset( double aXoff, double aYoff )
    {
        tx  = aXoff;
        ty  = aYoff;
    }

    double GetLayerZ( LAYER_NUM aLayer )
    {
        if( aLayer >= NB_LAYERS )
            return 0;

        return layer_z[ aLayer ];
    }

    void SetLayerZ( LAYER_NUM aLayer, double aValue )
    {
        layer_z[aLayer] = aValue;
    }

    void SetMaxDev( double dev )
    {
        holes.SetMaxDev( dev );
        board.SetMaxDev( dev );
        top_copper.SetMaxDev( dev );
        bot_copper.SetMaxDev( dev );
        top_silk.SetMaxDev( dev );
        bot_silk.SetMaxDev( dev );
        top_tin.SetMaxDev( dev );
        bot_tin.SetMaxDev( dev );
    }
};


// static var. for dealing with text
namespace VRMLEXPORT
{
    static MODEL_VRML* model_vrml;
    bool GetLayer( MODEL_VRML& aModel, LAYER_NUM layer, VRML_LAYER** vlayer );
}


// select the VRML layer object to draw on; return true if
// a layer has been selected.
bool VRMLEXPORT::GetLayer( MODEL_VRML& aModel, LAYER_NUM layer, VRML_LAYER** vlayer )
{
    switch( layer )
    {
    case FIRST_COPPER_LAYER:
        *vlayer = &aModel.bot_copper;
        break;

    case LAST_COPPER_LAYER:
        *vlayer = &aModel.top_copper;
        break;

    case SILKSCREEN_N_BACK:
        *vlayer = &aModel.bot_silk;
        break;

    case SILKSCREEN_N_FRONT:
        *vlayer = &aModel.top_silk;
        break;

    default:
        return false;
    }

    return true;
}


static void write_triangle_bag( FILE* output_file, VRML_COLOR& color,
        VRML_LAYER* layer, bool plane, bool top,
        double top_z, double bottom_z )
{
    /* A lot of nodes are not required, but blender sometimes chokes
     * without them */
    static const char* shape_boiler[] =
    {
        "Transform {\n",
        "  children [\n",
        "    Group {\n",
        "      children [\n",
        "        Shape {\n",
        "          appearance Appearance {\n",
        "            material Material {\n",
        0,                                      // Material marker
        "            }\n",
        "          }\n",
        "          geometry IndexedFaceSet {\n",
        "            solid TRUE\n",
        "            coord Coordinate {\n",
        "              point [\n",
        0,                                      // Coordinates marker
        "              ]\n",
        "            }\n",
        "            coordIndex [\n",
        0,                                      // Index marker
        "            ]\n",
        "          }\n",
        "        }\n",
        "      ]\n",
        "    }\n",
        "  ]\n",
        "}\n",
        0    // End marker
    };

    int marker_found = 0, lineno = 0;

    while( marker_found < 4 )
    {
        if( shape_boiler[lineno] )
            fputs( shape_boiler[lineno], output_file );
        else
        {
            marker_found++;

            switch( marker_found )
            {
            case 1:    // Material marker
                fprintf( output_file,
                        "              diffuseColor %g %g %g\n",
                         color.diffuse_red,
                         color.diffuse_grn,
                         color.diffuse_blu );
                fprintf( output_file,
                        "              specularColor %g %g %g\n",
                         color.spec_red,
                         color.spec_grn,
                         color.spec_blu );
                fprintf( output_file,
                        "              emissiveColor %g %g %g\n",
                         color.emit_red,
                         color.emit_grn,
                         color.emit_blu );
                fprintf( output_file,
                         "              ambientIntensity %g\n", color.ambient );
                fprintf( output_file,
                         "              transparency %g\n", color.transp );
                fprintf( output_file,
                         "              shininess %g\n", color.shiny );
                break;

            case 2:

                if( plane )
                    layer->WriteVertices( top_z, output_file );
                else
                    layer->Write3DVertices( top_z, bottom_z, output_file );

                fprintf( output_file, "\n" );
                break;

            case 3:

                if( plane )
                    layer->WriteIndices( top, output_file );
                else
                    layer->Write3DIndices( output_file );

                fprintf( output_file, "\n" );
                break;

            default:
                break;
            }
        }

        lineno++;
    }
}


static void write_layers( MODEL_VRML& aModel, FILE* output_file, BOARD* aPcb )
{
    // VRML_LAYER board;
    aModel.board.Tesselate( &aModel.holes );
    double brdz = aModel.board_thickness / 2.0 - 40000 * aModel.scale;
    write_triangle_bag( output_file, aModel.GetColor( VRML_COLOR_PCB ),
            &aModel.board, false, false, brdz, -brdz );

    // VRML_LAYER top_copper;
    aModel.top_copper.Tesselate( &aModel.holes );
    write_triangle_bag( output_file, aModel.GetColor( VRML_COLOR_TRACK ),
            &aModel.top_copper, true, true,
            aModel.GetLayerZ( LAST_COPPER_LAYER ), 0 );

    // VRML_LAYER top_tin;
    aModel.top_tin.Tesselate( &aModel.holes );
    write_triangle_bag( output_file, aModel.GetColor( VRML_COLOR_TIN ),
                        &aModel.top_tin, true, true,
                        aModel.GetLayerZ( LAST_COPPER_LAYER ), 0 );

    // VRML_LAYER bot_copper;
    aModel.bot_copper.Tesselate( &aModel.holes );
    write_triangle_bag( output_file, aModel.GetColor( VRML_COLOR_TRACK ),
            &aModel.bot_copper, true, false,
            aModel.GetLayerZ( FIRST_COPPER_LAYER ), 0 );

    // VRML_LAYER bot_tin;
    aModel.bot_tin.Tesselate( &aModel.holes );
    write_triangle_bag( output_file, aModel.GetColor( VRML_COLOR_TIN ),
                        &aModel.bot_tin, true, false,
                        aModel.GetLayerZ( FIRST_COPPER_LAYER ), 0 );

    // VRML_LAYER top_silk;
    aModel.top_silk.Tesselate( &aModel.holes );
    write_triangle_bag( output_file, aModel.GetColor( VRML_COLOR_SILK ),
            &aModel.top_silk, true, true,
            aModel.GetLayerZ( SILKSCREEN_N_FRONT ), 0 );

    // VRML_LAYER bot_silk;
    aModel.bot_silk.Tesselate( &aModel.holes );
    write_triangle_bag( output_file, aModel.GetColor( VRML_COLOR_SILK ),
            &aModel.bot_silk, true, false,
            aModel.GetLayerZ( SILKSCREEN_N_BACK ), 0 );
}


static void compute_layer_Zs( MODEL_VRML& aModel, BOARD* pcb )
{
    int copper_layers = pcb->GetCopperLayerCount();

    // We call it 'layer' thickness, but it's the whole board thickness!
    aModel.board_thickness = pcb->GetDesignSettings().GetBoardThickness() * aModel.scale;
    double half_thickness = aModel.board_thickness / 2;

    // Compute each layer's Z value, more or less like the 3d view
    for( LAYER_NUM i = FIRST_LAYER; i <= LAYER_N_FRONT; ++i )
    {
        if( i < copper_layers )
            aModel.SetLayerZ( i, aModel.board_thickness * i / (copper_layers - 1) - half_thickness );
        else
            aModel.SetLayerZ( i, half_thickness );  // component layer
    }

    /* To avoid rounding interference, we apply an epsilon to each
     * successive layer */
    double epsilon_z = Millimeter2iu( 0.02 ) * aModel.scale;
    aModel.SetLayerZ( SOLDERPASTE_N_BACK, -half_thickness - epsilon_z * 4 );
    aModel.SetLayerZ( ADHESIVE_N_BACK, -half_thickness - epsilon_z * 3 );
    aModel.SetLayerZ( SILKSCREEN_N_BACK, -half_thickness - epsilon_z * 2 );
    aModel.SetLayerZ( SOLDERMASK_N_BACK, -half_thickness - epsilon_z );
    aModel.SetLayerZ( SOLDERMASK_N_FRONT, half_thickness + epsilon_z );
    aModel.SetLayerZ( SILKSCREEN_N_FRONT, half_thickness + epsilon_z * 2 );
    aModel.SetLayerZ( ADHESIVE_N_FRONT, half_thickness + epsilon_z * 3 );
    aModel.SetLayerZ( SOLDERPASTE_N_FRONT, half_thickness + epsilon_z * 4 );
    aModel.SetLayerZ( DRAW_N, half_thickness + epsilon_z * 5 );
    aModel.SetLayerZ( COMMENT_N, half_thickness + epsilon_z * 6 );
    aModel.SetLayerZ( ECO1_N, half_thickness + epsilon_z * 7 );
    aModel.SetLayerZ( ECO2_N, half_thickness + epsilon_z * 8 );
    aModel.SetLayerZ( EDGE_N, 0 );
}


static void export_vrml_line( MODEL_VRML& aModel, LAYER_NUM layer,
        double startx, double starty,
        double endx, double endy, double width )
{
    VRML_LAYER* vlayer;

    if( !VRMLEXPORT::GetLayer( aModel, layer, &vlayer ) )
        return;

    starty = -starty;
    endy = -endy;

    double  angle   = atan2( endy - starty, endx - startx );
    double  length  = Distance( startx, starty, endx, endy ) + width;
    double  cx  = ( startx + endx ) / 2.0;
    double  cy  = ( starty + endy ) / 2.0;

    vlayer->AddSlot( cx, cy, length, width, angle, 1, false );
}


static void export_vrml_circle( MODEL_VRML& aModel, LAYER_NUM layer,
        double startx, double starty,
        double endx, double endy, double width )
{
    VRML_LAYER* vlayer;

    if( !VRMLEXPORT::GetLayer( aModel, layer, &vlayer ) )
        return;

    starty = -starty;
    endy = -endy;

    double hole, radius;

    radius = Distance( startx, starty, endx, endy ) + ( width / 2);
    hole = radius - width;

    vlayer->AddCircle( startx, starty, radius, 1, false );

    if( hole > 0.0001 )
    {
        vlayer->AddCircle( startx, starty, hole, 1, true );
    }
}


static void export_vrml_arc( MODEL_VRML& aModel, LAYER_NUM layer,
        double centerx, double centery,
        double arc_startx, double arc_starty,
        double width, double arc_angle )
{
    VRML_LAYER* vlayer;

    if( !VRMLEXPORT::GetLayer( aModel, layer, &vlayer ) )
        return;

    centery = -centery;
    arc_starty = -arc_starty;

    arc_angle *= -M_PI / 180;

    vlayer->AddArc( centerx, centery, arc_startx, arc_starty,
            width, arc_angle, 1, false );
}


static void export_vrml_drawsegment( MODEL_VRML& aModel, DRAWSEGMENT* drawseg )
{
    LAYER_NUM layer = drawseg->GetLayer();
    double  w   = drawseg->GetWidth() * aModel.scale;
    double  x   = drawseg->GetStart().x * aModel.scale + aModel.tx;
    double  y   = drawseg->GetStart().y * aModel.scale + aModel.ty;
    double  xf  = drawseg->GetEnd().x * aModel.scale + aModel.tx;
    double  yf  = drawseg->GetEnd().y * aModel.scale + aModel.ty;

    // Items on the edge layer are handled elsewhere; just return
    if( layer == EDGE_N )
        return;

    switch( drawseg->GetShape() )
    {
    case S_ARC:
        export_vrml_arc( aModel, layer,
                (double) drawseg->GetCenter().x,
                (double) drawseg->GetCenter().y,
                (double) drawseg->GetArcStart().x,
                (double) drawseg->GetArcStart().y,
                w, drawseg->GetAngle() / 10 );
        break;

    case S_CIRCLE:
        export_vrml_circle( aModel, layer, x, y, xf, yf, w );
        break;

    default:
        export_vrml_line( aModel, layer, x, y, xf, yf, w );
        break;
    }
}


/* C++ doesn't have closures and neither continuation forms... this is
 * for coupling the vrml_text_callback with the common parameters */
static void vrml_text_callback( int x0, int y0, int xf, int yf )
{
    LAYER_NUM s_text_layer = VRMLEXPORT::model_vrml->s_text_layer;
    int s_text_width = VRMLEXPORT::model_vrml->s_text_width;
    double  scale = VRMLEXPORT::model_vrml->scale;
    double  tx  = VRMLEXPORT::model_vrml->tx;
    double  ty  = VRMLEXPORT::model_vrml->ty;

    export_vrml_line( *VRMLEXPORT::model_vrml, s_text_layer,
            x0 * scale + tx, y0 * scale + ty,
            xf * scale + tx, yf * scale + ty,
            s_text_width * scale );
}


static void export_vrml_pcbtext( MODEL_VRML& aModel, TEXTE_PCB* text )
{
    VRMLEXPORT::model_vrml->s_text_layer    = text->GetLayer();
    VRMLEXPORT::model_vrml->s_text_width    = text->GetThickness();

    wxSize size = text->GetSize();

    if( text->IsMirrored() )
        NEGATE( size.x );

    if( text->IsMultilineAllowed() )
    {
        wxPoint pos = text->GetTextPosition();
        wxArrayString* list = wxStringSplit( text->GetText(), '\n' );
        wxPoint offset;

        offset.y = text->GetInterline();

        RotatePoint( &offset, text->GetOrientation() );

        for( unsigned i = 0; i<list->Count(); i++ )
        {
            wxString txt = list->Item( i );
            DrawGraphicText( NULL, NULL, pos, BLACK,
                    txt, text->GetOrientation(), size,
                    text->GetHorizJustify(), text->GetVertJustify(),
                    text->GetThickness(), text->IsItalic(),
                    true,
                    vrml_text_callback );
            pos += offset;
        }

        delete (list);
    }
    else
    {
        DrawGraphicText( NULL, NULL, text->GetTextPosition(), BLACK,
                text->GetText(), text->GetOrientation(), size,
                text->GetHorizJustify(), text->GetVertJustify(),
                text->GetThickness(), text->IsItalic(),
                true,
                vrml_text_callback );
    }
}


static void export_vrml_drawings( MODEL_VRML& aModel, BOARD* pcb )
{
    // draw graphic items
    for( EDA_ITEM* drawing = pcb->m_Drawings; drawing != 0; drawing = drawing->Next() )
    {
        LAYER_NUM layer = ( (DRAWSEGMENT*) drawing )->GetLayer();

        if( layer != FIRST_COPPER_LAYER && layer != LAST_COPPER_LAYER
            && layer != SILKSCREEN_N_BACK && layer != SILKSCREEN_N_FRONT )
            continue;

        switch( drawing->Type() )
        {
        case PCB_LINE_T:
            export_vrml_drawsegment( aModel, (DRAWSEGMENT*) drawing );
            break;

        case PCB_TEXT_T:
            export_vrml_pcbtext( aModel, (TEXTE_PCB*) drawing );
            break;

        default:
            break;
        }
    }
}


// board edges and cutouts
static void export_vrml_board( MODEL_VRML& aModel, BOARD* pcb )
{
    CPOLYGONS_LIST  bufferPcbOutlines;      // stores the board main outlines
    CPOLYGONS_LIST  allLayerHoles;          // Contains through holes, calculated only once

    allLayerHoles.reserve( 20000 );

    // Build a polygon from edge cut items
    wxString msg;

    if( !pcb->GetBoardPolygonOutlines( bufferPcbOutlines,
                allLayerHoles, &msg ) )
    {
        msg << wxT( "\n\n" ) <<
        _( "Unable to calculate the board outlines;\n"
           "fall back to using the board boundary box." );
        wxMessageBox( msg );
    }

    double  scale = aModel.scale;
    double  dx  = aModel.tx;
    double  dy  = aModel.ty;

    int i = 0;
    int seg;

    // deal with the solid outlines
    int nvert = bufferPcbOutlines.GetCornersCount();

    while( i < nvert )
    {
        seg = aModel.board.NewContour();

        if( seg < 0 )
        {
            msg << wxT( "\n\n" ) <<
                _( "VRML Export Failed:\nCould not add outline to contours." );
            wxMessageBox( msg );

            return;
        }

        while( i < nvert )
        {
            aModel.board.AddVertex( seg, bufferPcbOutlines[i].x * scale + dx,
                    -(bufferPcbOutlines[i].y * scale + dy) );

            if( bufferPcbOutlines[i].end_contour )
                break;

            ++i;
        }

        aModel.board.EnsureWinding( seg, false );
        ++i;
    }

    // deal with the holes
    nvert = allLayerHoles.GetCornersCount();

    i = 0;
    while( i < nvert )
    {
        seg = aModel.holes.NewContour();

        if( seg < 0 )
        {
            msg << wxT( "\n\n" ) <<
            _( "VRML Export Failed:\nCould not add holes to contours." );
            wxMessageBox( msg );

            return;
        }

        while( i < nvert )
        {
            aModel.holes.AddVertex( seg, allLayerHoles[i].x * scale + dx,
                                    -(allLayerHoles[i].y * scale + dy) );

            if( allLayerHoles[i].end_contour )
                break;

            ++i;
        }

        aModel.holes.EnsureWinding( seg, true );
        ++i;
    }
}


static void export_round_padstack( MODEL_VRML& aModel, BOARD* pcb,
        double x, double y, double r,
        LAYER_NUM bottom_layer, LAYER_NUM top_layer,
        double hole )
{
    LAYER_NUM layer = top_layer;
    bool thru = true;

    // if not a thru hole do not put a hole in the board
    if( top_layer != LAST_COPPER_LAYER || bottom_layer != FIRST_COPPER_LAYER )
        thru = false;

    while( 1 )
    {
        if( layer == FIRST_COPPER_LAYER )
        {
            aModel.bot_copper.AddCircle( x, -y, r, 1 );

            if( hole > 0 )
            {
                if( thru )
                    aModel.holes.AddCircle( x, -y, hole, 1, true );
                else
                    aModel.bot_copper.AddCircle( x, -y, hole, 1, true );
            }
        }
        else if( layer == LAST_COPPER_LAYER )
        {
            aModel.top_copper.AddCircle( x, -y, r, 1 );

            if( hole > 0 )
            {
                if( thru )
                    aModel.holes.AddCircle( x, -y, hole, 1, true );
                else
                    aModel.top_copper.AddCircle( x, -y, hole, 1, true );
            }
        }

        if( layer == bottom_layer )
            break;

        layer = bottom_layer;
    }
}


static void export_vrml_via( MODEL_VRML& aModel, BOARD* pcb, SEGVIA* via )
{
    double x, y, r, hole;
    LAYER_NUM top_layer, bottom_layer;

    hole = via->GetDrillValue() * aModel.scale / 2.0;
    r   = via->GetWidth() * aModel.scale / 2.0;
    x   = via->GetStart().x * aModel.scale + aModel.tx;
    y   = via->GetStart().y * aModel.scale + aModel.ty;
    via->LayerPair( &top_layer, &bottom_layer );

    // do not render a buried via
    if( top_layer != LAST_COPPER_LAYER && bottom_layer != FIRST_COPPER_LAYER )
        return;

    // Export the via padstack
    export_round_padstack( aModel, pcb, x, y, r, bottom_layer, top_layer, hole );
}


static void export_vrml_tracks( MODEL_VRML& aModel, BOARD* pcb )
{
    for( TRACK* track = pcb->m_Track; track != NULL; track = track->Next() )
    {
        if( track->Type() == PCB_VIA_T )
        {
            export_vrml_via( aModel, pcb, (SEGVIA*) track );
        }
        else if( track->GetLayer() == FIRST_COPPER_LAYER
                 || track->GetLayer() == LAST_COPPER_LAYER )
            export_vrml_line( aModel, track->GetLayer(),
                    track->GetStart().x * aModel.scale + aModel.tx,
                    track->GetStart().y * aModel.scale + aModel.ty,
                    track->GetEnd().x * aModel.scale + aModel.tx,
                    track->GetEnd().y * aModel.scale + aModel.ty,
                    track->GetWidth() * aModel.scale );
    }
}


static void export_vrml_zones( MODEL_VRML& aModel, BOARD* aPcb )
{

    double scale = aModel.scale;
    double dx = aModel.tx;
    double dy = aModel.ty;

    double x, y;

    for( int ii = 0; ii < aPcb->GetAreaCount(); ii++ )
    {
        ZONE_CONTAINER* zone = aPcb->GetArea( ii );

        VRML_LAYER* vl;

        if( !VRMLEXPORT::GetLayer( aModel, zone->GetLayer(), &vl ) )
            continue;

        if( !zone->IsFilled() )
        {
            zone->SetFillMode( 0 ); // use filled polygons
            zone->BuildFilledSolidAreasPolygons( aPcb );
        }
        const CPOLYGONS_LIST& poly = zone->GetFilledPolysList();

        int nvert = poly.GetCornersCount();
        int i = 0;

        while( i < nvert )
        {
            int seg = vl->NewContour();
            bool first = true;

            if( seg < 0 )
                break;

            while( i < nvert )
            {
                x = poly.GetX(i) * scale + dx;
                y = -(poly.GetY(i) * scale + dy);
                vl->AddVertex( seg, x, y );

                if( poly.IsEndContour(i) )
                    break;

                ++i;
            }

            // KiCad ensures that the first polygon is the outline
            // and all others are holes
             vl->EnsureWinding( seg, first ? false : true );

            if( first )
                first = false;

            ++i;
        }
    }
}


static void export_vrml_text_module( TEXTE_MODULE* module )
{
    if( module->IsVisible() )
    {
        wxSize size = module->GetSize();

        if( module->IsMirrored() )
            NEGATE( size.x );  // Text is mirrored

        VRMLEXPORT::model_vrml->s_text_layer    = module->GetLayer();
        VRMLEXPORT::model_vrml->s_text_width    = module->GetThickness();

        DrawGraphicText( NULL, NULL, module->GetTextPosition(), BLACK,
                module->GetText(), module->GetDrawRotation(), size,
                module->GetHorizJustify(), module->GetVertJustify(),
                module->GetThickness(), module->IsItalic(),
                true,
                vrml_text_callback );
    }
}


static void export_vrml_edge_module( MODEL_VRML& aModel, EDGE_MODULE* aOutline,
                                     double aOrientation )
{
    LAYER_NUM layer = aOutline->GetLayer();
    double  x   = aOutline->GetStart().x * aModel.scale + aModel.tx;
    double  y   = aOutline->GetStart().y * aModel.scale + aModel.ty;
    double  xf  = aOutline->GetEnd().x * aModel.scale + aModel.tx;
    double  yf  = aOutline->GetEnd().y * aModel.scale + aModel.ty;
    double  w   = aOutline->GetWidth() * aModel.scale;

    switch( aOutline->GetShape() )
    {
    case S_SEGMENT:
        export_vrml_line( aModel, layer, x, y, xf, yf, w );
        break;

    case S_ARC:
        export_vrml_arc( aModel, layer, x, y, xf, yf, w, aOutline->GetAngle() / 10 );
        break;

    case S_CIRCLE:
        export_vrml_circle( aModel, layer, x, y, xf, yf, w );
        break;

    case S_POLYGON:
        {
            VRML_LAYER* vl;

            if( !VRMLEXPORT::GetLayer( aModel, layer, &vl ) )
                break;

            int nvert = aOutline->GetPolyPoints().size();
            int i = 0;

            if( nvert < 3 ) break;

            int seg = vl->NewContour();

            if( seg < 0 )
                break;

            while( i < nvert )
            {
                CPolyPt corner( aOutline->GetPolyPoints()[i] );
                RotatePoint( &corner.x, &corner.y, aOrientation );
                corner.x += aOutline->GetPosition().x;
                corner.y += aOutline->GetPosition().y;

                x = corner.x * aModel.scale + aModel.tx;
                y = - ( corner.y * aModel.scale + aModel.ty );
                vl->AddVertex( seg, x, y );

                ++i;
            }
            vl->EnsureWinding( seg, false );
        }
        break;

    default:
        break;
    }
}


static void export_vrml_padshape( MODEL_VRML& aModel, VRML_LAYER* aLayer,
                                  VRML_LAYER* aTinLayer, D_PAD* aPad )
{
    // The (maybe offset) pad position
    wxPoint pad_pos = aPad->ShapePos();
    double  pad_x   = pad_pos.x * aModel.scale + aModel.tx;
    double  pad_y   = pad_pos.y * aModel.scale + aModel.ty;
    wxSize  pad_delta = aPad->GetDelta();

    double  pad_dx  = pad_delta.x * aModel.scale / 2.0;
    double  pad_dy  = pad_delta.y * aModel.scale / 2.0;

    double  pad_w   = aPad->GetSize().x * aModel.scale / 2.0;
    double  pad_h   = aPad->GetSize().y * aModel.scale / 2.0;

    switch( aPad->GetShape() )
    {
    case PAD_CIRCLE:
        aLayer->AddCircle( pad_x, -pad_y, pad_w, 1, true );
        aTinLayer->AddCircle( pad_x, -pad_y, pad_w, 1, false );
        break;

    case PAD_OVAL:
        aLayer->AddSlot( pad_x, -pad_y, pad_w * 2.0, pad_h * 2.0,
                DECIDEG2RAD( aPad->GetOrientation() ), 1, true );
        aTinLayer->AddSlot( pad_x, -pad_y, pad_w * 2.0, pad_h * 2.0,
                         DECIDEG2RAD( aPad->GetOrientation() ), 1, false );
        break;

    case PAD_RECT:
        // Just to be sure :D
        pad_dx  = 0;
        pad_dy  = 0;

    case PAD_TRAPEZOID:
        {
            double coord[8] =
            {
                -pad_w + pad_dy, -pad_h - pad_dx,
                -pad_w - pad_dy, pad_h + pad_dx,
                +pad_w - pad_dy, -pad_h + pad_dx,
                +pad_w + pad_dy, pad_h - pad_dx
            };

            for( int i = 0; i < 4; i++ )
            {
                RotatePoint( &coord[i * 2], &coord[i * 2 + 1], aPad->GetOrientation() );
                coord[i * 2] += pad_x;
                coord[i * 2 + 1] += pad_y;
            }

            int lines = aLayer->NewContour();

            if( lines < 0 )
                return;

            aLayer->AddVertex( lines, coord[2], -coord[3] );
            aLayer->AddVertex( lines, coord[6], -coord[7] );
            aLayer->AddVertex( lines, coord[4], -coord[5] );
            aLayer->AddVertex( lines, coord[0], -coord[1] );
            aLayer->EnsureWinding( lines, true );

            lines = aTinLayer->NewContour();

            if( lines < 0 )
                return;

            aTinLayer->AddVertex( lines, coord[0], -coord[1] );
            aTinLayer->AddVertex( lines, coord[4], -coord[5] );
            aTinLayer->AddVertex( lines, coord[6], -coord[7] );
            aTinLayer->AddVertex( lines, coord[2], -coord[3] );
            aTinLayer->EnsureWinding( lines, false );
        }
        break;

    default:
        ;
    }
}


static void export_vrml_pad( MODEL_VRML& aModel, BOARD* pcb, D_PAD* aPad )
{
    double  hole_drill_w    = (double) aPad->GetDrillSize().x * aModel.scale / 2.0;
    double  hole_drill_h    = (double) aPad->GetDrillSize().y * aModel.scale / 2.0;
    double  hole_drill = std::min( hole_drill_w, hole_drill_h );
    double  hole_x  = aPad->GetPosition().x * aModel.scale + aModel.tx;
    double  hole_y  = aPad->GetPosition().y * aModel.scale + aModel.ty;

    // Export the hole on the edge layer
    if( hole_drill > 0 )
    {
        if( aPad->GetDrillShape() == PAD_DRILL_OBLONG )
        {
            // Oblong hole (slot)
            aModel.holes.AddSlot( hole_x, -hole_y, hole_drill_w * 2.0, hole_drill_h * 2.0,
                    DECIDEG2RAD( aPad->GetOrientation() ), 1, true );
        }
        else
        {
            // Drill a round hole
            aModel.holes.AddCircle( hole_x, -hole_y, hole_drill, 1, true );
        }
    }

    // The pad proper, on the selected layers
    LAYER_MSK layer_mask = aPad->GetLayerMask();

    if( layer_mask & LAYER_BACK )
    {
        export_vrml_padshape( aModel, &aModel.bot_copper, &aModel.bot_tin, aPad );
    }

    if( layer_mask & LAYER_FRONT )
    {
        export_vrml_padshape( aModel, &aModel.top_copper, &aModel.top_tin, aPad );
    }
}


// From axis/rot to quaternion
static void build_quat( double x, double y, double z, double a, double q[4] )
{
    double sina = sin( a / 2 );

    q[0] = x * sina;
    q[1] = y * sina;
    q[2] = z * sina;
    q[3] = cos( a / 2 );
}


// From quaternion to axis/rot
static void from_quat( double q[4], double rot[4] )
{
    rot[3] = acos( q[3] ) * 2;

    for( int i = 0; i < 3; i++ )
    {
        rot[i] = q[i] / sin( rot[3] / 2 );
    }
}


// Quaternion composition
static void compose_quat( double q1[4], double q2[4], double qr[4] )
{
    double tmp[4];

    tmp[0] = q2[3] * q1[0] + q2[0] * q1[3] + q2[1] * q1[2] - q2[2] * q1[1];
    tmp[1] = q2[3] * q1[1] + q2[1] * q1[3] + q2[2] * q1[0] - q2[0] * q1[2];
    tmp[2] = q2[3] * q1[2] + q2[2] * q1[3] + q2[0] * q1[1] - q2[1] * q1[0];
    tmp[3] = q2[3] * q1[3] - q2[0] * q1[0] - q2[1] * q1[1] - q2[2] * q1[2];

    qr[0] = tmp[0];
    qr[1] = tmp[1];
    qr[2] = tmp[2];
    qr[3] = tmp[3];
}


static void export_vrml_module( MODEL_VRML& aModel, BOARD* aPcb, MODULE* aModule,
        FILE* aOutputFile,
        double aVRMLModelsToBiu,
        bool aExport3DFiles, const wxString& a3D_Subdir )
{
    // Reference and value
    if( aModule->Reference().IsVisible() )
        export_vrml_text_module( &aModule->Reference() );

    if( aModule->Value().IsVisible() )
        export_vrml_text_module( &aModule->Value() );

    // Export module edges
    for( EDA_ITEM* item = aModule->GraphicalItems(); item != NULL; item = item->Next() )
    {
        switch( item->Type() )
        {
        case PCB_MODULE_TEXT_T:
            export_vrml_text_module( dynamic_cast<TEXTE_MODULE*>( item ) );
            break;

        case PCB_MODULE_EDGE_T:
            export_vrml_edge_module( aModel, dynamic_cast<EDGE_MODULE*>( item ),
                                     aModule->GetOrientation() );
            break;

        default:
            break;
        }
    }

    // Export pads
    for( D_PAD* pad = aModule->Pads(); pad; pad = pad->Next() )
        export_vrml_pad( aModel, aPcb, pad );

    bool isFlipped = aModule->GetLayer() == LAYER_N_BACK;

    // Export the object VRML model(s)
    for( S3D_MASTER* vrmlm = aModule->Models();  vrmlm;  vrmlm = vrmlm->Next() )
    {
        if( !vrmlm->Is3DType( S3D_MASTER::FILE3D_VRML ) )
            continue;

        wxString fname = vrmlm->GetShape3DFullFilename();

        fname.Replace( wxT( "\\" ), wxT( "/" ) );
        wxString source_fname = fname;

        if( aExport3DFiles )
        {
            // Change illegal characters in filenames
            ChangeIllegalCharacters( fname, true );
            fname = a3D_Subdir + wxT( "/" ) + fname;

            if( !wxFileExists( fname ) )
                wxCopyFile( source_fname, fname );
        }

        /* Calculate 3D shape rotation:
         * this is the rotation parameters, with an additional 180 deg rotation
         * for footprints that are flipped
         * When flipped, axis rotation is the horizontal axis (X axis)
         */
        double rotx = -vrmlm->m_MatRotation.x;
        double roty = -vrmlm->m_MatRotation.y;
        double rotz = -vrmlm->m_MatRotation.z;

        if( isFlipped )
        {
            rotx += 180.0;
            NEGATE( roty );
            NEGATE( rotz );
        }

        // Do some quaternion munching
        double q1[4], q2[4], rot[4];
        build_quat( 1, 0, 0, DEG2RAD( rotx ), q1 );
        build_quat( 0, 1, 0, DEG2RAD( roty ), q2 );
        compose_quat( q1, q2, q1 );
        build_quat( 0, 0, 1, DEG2RAD( rotz ), q2 );
        compose_quat( q1, q2, q1 );

        // Note here aModule->GetOrientation() is in 0.1 degrees,
        // so module rotation has to be converted to radians
        build_quat( 0, 0, 1, DECIDEG2RAD( aModule->GetOrientation() ), q2 );
        compose_quat( q1, q2, q1 );
        from_quat( q1, rot );

        fprintf( aOutputFile, "Transform {\n" );

        // A null rotation would fail the acos!
        if( rot[3] != 0.0 )
        {
            fprintf( aOutputFile, "  rotation %g %g %g %g\n", rot[0], rot[1], rot[2], rot[3] );
        }

        // adjust 3D shape local offset position
        // they are given in inch, so they are converted in board IU.
        double offsetx = vrmlm->m_MatPosition.x * IU_PER_MILS * 1000.0;
        double offsety = vrmlm->m_MatPosition.y * IU_PER_MILS * 1000.0;
        double offsetz = vrmlm->m_MatPosition.z * IU_PER_MILS * 1000.0;

        if( isFlipped )
            NEGATE( offsetz );
        else // In normal mode, Y axis is reversed in Pcbnew.
            NEGATE( offsety );

        RotatePoint( &offsetx, &offsety, aModule->GetOrientation() );

        fprintf( aOutputFile, "  translation %g %g %g\n",
                (offsetx + aModule->GetPosition().x) * aModel.scale + aModel.tx,
                -(offsety + aModule->GetPosition().y) * aModel.scale - aModel.ty,
                (offsetz * aModel.scale ) + aModel.GetLayerZ( aModule->GetLayer() ) );

        fprintf( aOutputFile, "  scale %g %g %g\n",
                vrmlm->m_MatScale.x * aVRMLModelsToBiu,
                vrmlm->m_MatScale.y * aVRMLModelsToBiu,
                vrmlm->m_MatScale.z * aVRMLModelsToBiu );

        if( fname.EndsWith( wxT( "x3d" ) ) )
        {
            X3D_MODEL_PARSER* parser = new X3D_MODEL_PARSER( vrmlm );

            if( parser )
            {
                // embed x3d model in vrml format
                parser->Load( fname );
                fprintf( aOutputFile,
                        "  children [\n %s ]\n", TO_UTF8( parser->VRML_representation() ) );
                fprintf( aOutputFile, "  }\n" );
                delete parser;
            }
        }
        else
        {
            fprintf( aOutputFile,
                    "  children [\n    Inline {\n      url \"%s\"\n    } ]\n",
                    TO_UTF8( fname ) );
            fprintf( aOutputFile, "  }\n" );
        }
    }
}


bool PCB_EDIT_FRAME::ExportVRML_File( const wxString& aFullFileName,
        double aMMtoWRMLunit, bool aExport3DFiles,
        const wxString& a3D_Subdir )
{
    wxString    msg;
    FILE*       output_file;
    BOARD*      pcb = GetBoard();

    MODEL_VRML model3d;

    VRMLEXPORT::model_vrml = &model3d;

    output_file = wxFopen( aFullFileName, wxT( "wt" ) );

    if( output_file == NULL )
        return false;

    // Switch the locale to standard C (needed to print floating point numbers like 1.3)
    SetLocaleTo_C_standard();

    // Begin with the usual VRML boilerplate
    wxString name = aFullFileName;

    name.Replace( wxT( "\\" ), wxT( "/" ) );
    ChangeIllegalCharacters( name, false );
    fprintf( output_file, "#VRML V2.0 utf8\n"
                          "WorldInfo {\n"
                          "  title \"%s - Generated by Pcbnew\"\n"
                          "}\n", TO_UTF8( name ) );

    // Global VRML scale to export to a different scale.
    model3d.scale = aMMtoWRMLunit / MM_PER_IU;

    // Set the mechanical deviation limit (in this case 0.02mm)
    // XXX - NOTE: the value should be set via the GUI
    model3d.SetMaxDev( 20000 * model3d.scale );

    fprintf( output_file, "Transform {\n" );

    // compute the offset to center the board on (0, 0, 0)
    // XXX - NOTE: we should allow the user a GUI option to specify the offset
    EDA_RECT bbbox = pcb->ComputeBoundingBox();

    model3d.SetOffset( -model3d.scale * bbbox.Centre().x, -model3d.scale * bbbox.Centre().y );

    fprintf( output_file, "  children [\n" );

    // Preliminary computation: the z value for each layer
    compute_layer_Zs( model3d, pcb );

    // board edges and cutouts
    export_vrml_board( model3d, pcb );

    // Drawing and text on the board
    export_vrml_drawings( model3d, pcb );

    // Export vias and trackage
    export_vrml_tracks( model3d, pcb );

    // Export zone fills
    export_vrml_zones( model3d, pcb);

    /* scaling factor to convert 3D models to board units (decimils)
     * Usually we use Wings3D to create thems.
     * One can consider the 3D units is 0.1 inch (2.54 mm)
     * So the scaling factor from 0.1 inch to board units
     * is 2.54 * aMMtoWRMLunit
     */
    double wrml_3D_models_scaling_factor = 2.54 * aMMtoWRMLunit;

    // Export footprints
    for( MODULE* module = pcb->m_Modules; module != 0; module = module->Next() )
        export_vrml_module( model3d, pcb, module, output_file,
                wrml_3D_models_scaling_factor,
                aExport3DFiles, a3D_Subdir );

    // write out the board and all layers
    write_layers( model3d, output_file, pcb );

    // Close the outer 'transform' node
    fputs( "]\n}\n", output_file );

    // End of work
    fclose( output_file );
    SetLocaleTo_Default();       // revert to the current  locale

    return true;
}


/*
 * some characters cannot be used in filenames,
 * this function change them to "_"
 */
static void ChangeIllegalCharacters( wxString& aFileName, bool aDirSepIsIllegal )
{
    if( aDirSepIsIllegal )
        aFileName.Replace( wxT( "/" ), wxT( "_" ) );

    aFileName.Replace( wxT( " " ), wxT( "_" ) );
    aFileName.Replace( wxT( ":" ), wxT( "_" ) );
}
