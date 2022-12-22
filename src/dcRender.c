#include "dcRender.h"
#include <malloc.h>
#include <libetc.h>

void _dcRender_IncPrimitive(SDC_Render* render, size_t offset)
{
    render->nextPrimitive += offset;
}

void dcRender_Init(SDC_Render* render, int width, int height, CVECTOR bgColor, int orderingTableCount, int primitivesCount, EDC_Mode mode) {
	InitGeom();

    ResetGraph( 0 );
    SetGraphDebug(0);

    render->orderingTableCount = orderingTableCount;
    render->primitivesCount = primitivesCount;
    render->doubleBufferIndex = 0;
    render->width = width;
    render->height = height;
    render->bgColor = bgColor;

    render->orderingTable[0] = (u_long*)malloc3(sizeof(u_long) * orderingTableCount);
    render->orderingTable[1] = (u_long*)malloc3(sizeof(u_long) * orderingTableCount);
    
    ClearOTagR( render->orderingTable[0], orderingTableCount );
    ClearOTagR( render->orderingTable[1], orderingTableCount );
    
    render->primitives[0] = (u_char*)malloc3(sizeof(u_char) * primitivesCount);
    render->primitives[1] = (u_char*)malloc3(sizeof(u_char) * primitivesCount);
    
    render->nextPrimitive = render->primitives[0];

    SetDefDrawEnv( &render->drawEnvironment[0],    0, 0,      width, height );
    SetDefDrawEnv( &render->drawEnvironment[1],    0, height, width, height );
    SetDefDispEnv( &render->displayEnvironment[0], 0, height, width, height );
    SetDefDispEnv( &render->displayEnvironment[1], 0, 0,      width, height );

    setRGB0( &render->drawEnvironment[0], bgColor.r, bgColor.g, bgColor.b );
    render->drawEnvironment[0].isbg = 1;
    render->drawEnvironment[0].dtd = 1;
    render->displayEnvironment[0].isinter = 1;

    setRGB0( &render->drawEnvironment[1], bgColor.r, bgColor.g, bgColor.b );
    render->drawEnvironment[1].isbg = 1;
    render->drawEnvironment[1].dtd = 1;
    render->displayEnvironment[1].isinter = 1;

    SetDispMask(1);    
	// Set GTE offset (recommended method  of centering)
    SetGeomOffset(width>>1, height>>1);
	// Set screen depth (basically FOV control, W/2 works best)
	SetGeomScreen(width >> 1);
    // SetVideoMode(mode==RENDER_MODE_NTCS?MODE_NTSC:MODE_PAL);

    //Debug font (to remove)
    FntLoad(960, 256);
    SetDumpFnt(FntOpen(16, 16, 320, 64, 0, 512));
        
    PutDispEnv( &render->displayEnvironment[render->doubleBufferIndex] );
    PutDrawEnv( &render->drawEnvironment[render->doubleBufferIndex] );
}

void dcRender_SwapBuffers(SDC_Render* render) {
    VSync( 0 );
    DrawSync( 0 );
    SetDispMask( 1 );
    
    render->doubleBufferIndex = !render->doubleBufferIndex;
    
    ClearImage(&render->drawEnvironment[render->doubleBufferIndex].clip, render->bgColor.r, render->bgColor.g, render->bgColor.b);

    PutDrawEnv( &render->drawEnvironment[render->doubleBufferIndex] );
    PutDispEnv( &render->displayEnvironment[render->doubleBufferIndex] );
    
    DrawOTag( render->orderingTable[!render->doubleBufferIndex]+(render->orderingTableCount-1) );
    FntFlush(-1); // Font Debug
    
    ClearOTagR( render->orderingTable[render->doubleBufferIndex], render->orderingTableCount );
    render->nextPrimitive = render->primitives[render->doubleBufferIndex];    
}

void dcRender_LoadTexture(TIM_IMAGE* tim, long* texture) {
    OpenTIM(texture);                            // Open the tim binary data, feed it the address of the data in memory
    ReadTIM(tim);                                // This read the header of the TIM data and sets the corresponding members of the TIM_IMAGE structure

    LoadImage( tim->prect, tim->paddr );        // Transfer the data from memory to VRAM at position prect.x, prect.y
    if( tim->mode & 0x8 ) {                     // check 4th bit       // If 4th bit == 1, TIM has a CLUT
        LoadImage( tim->crect, tim->caddr );    // Load it to VRAM at position crect.x, crect.y
    }
    DrawSync(0);                                // Wait for drawing to end
}

void dcRender_DrawSpriteRect(SDC_Render* render, const TIM_IMAGE *tim, const SVECTOR *pos, const RECT *rect, const DVECTOR *uv, const CVECTOR *color) {
    SPRT *sprt = (SPRT*)render->nextPrimitive;

    setSprt(sprt);
    setXY0(sprt, pos->vx, pos->vy);
    setWH(sprt, rect->w, rect->h);
    setRGB0(sprt, color->r, color->g, color->b);
    setUV0(sprt, uv->vx, uv->vy);
    setClut(sprt, tim->crect->x, tim->crect->y);

    addPrim(render->orderingTable[render->doubleBufferIndex], sprt);

    _dcRender_IncPrimitive(render, sizeof(SPRT));

    DR_TPAGE *tpri = (DR_TPAGE*)render->nextPrimitive;
    u_short tpage = getTPage(tim->mode, 0, tim->prect->x, tim->prect->y);
    setDrawTPage(tpri, 0, 0, tpage);
    addPrim(render->orderingTable[render->doubleBufferIndex], tpri);
    _dcRender_IncPrimitive(render, sizeof(DR_TPAGE));
}

void dcRender_DrawMesh(SDC_Render* render,  SDC_Mesh3D* mesh, MATRIX* transform, const CVECTOR *color) {
    u_long *orderingTable = render->orderingTable[render->doubleBufferIndex];
    int orderingTableCount = render->orderingTableCount;
    long p, otz, flg;
    int nclip;

    SetRotMatrix(transform);
    SetTransMatrix(transform);

    for (int i = 0; i < mesh->numIndices; i += 3) {               
        u_short index0 = mesh->indices[i];
        u_short index1 = mesh->indices[i+1];
        u_short index2 = mesh->indices[i+2];
        void *poly = render->nextPrimitive;    
        CVECTOR curr_color = {255, 255, 255};
        if(color) 
            curr_color = *color;

        switch(mesh->polygonVertexType)
        {
            case POLIGON_VERTEX:
            case POLIGON_VERTEX_COLOR:
            {
                POLY_F3* polyF3 = (POLY_F3*)poly;
                SetPolyF3(polyF3);
                if(mesh->polygonVertexType == POLIGON_VERTEX_COLOR ) {
                    SDC_VertexColor *vertexs = (SDC_VertexColor *)mesh->vertexs;
                    if(!color) 
                        curr_color = vertexs[index0].color;

                    nclip = RotAverageNclip3(&vertexs[index0].position, &vertexs[index1].position, &vertexs[index2].position,
                                            (long *)&polyF3->x0, (long *)&polyF3->x1, (long *)&polyF3->x2, &p, &otz, &flg);
                } else {
                    SDC_Vertex *vertexs = (SDC_Vertex *)mesh->vertexs;
                    nclip = RotAverageNclip3(&vertexs[index0].position, &vertexs[index1].position, &vertexs[index2].position,
                                            (long *)&polyF3->x0, (long *)&polyF3->x1, (long *)&polyF3->x2, &p, &otz, &flg);
                }
                setRGB0(polyF3, curr_color.r, curr_color.g, curr_color.b);

                if (nclip <= 0) continue;
                if ((otz <= 0) || (otz >= orderingTableCount)) continue;

				addPrim(orderingTable[otz], polyF3);
                _dcRender_IncPrimitive(render, sizeof(POLY_F3));
            }
            break;
            case POLIGON_VERTEX_COLOR_GSHADED:
            {
                POLY_G3* polyG3 = (POLY_G3*)poly;
                SDC_VertexColor *vertexs = (SDC_VertexColor *)mesh->vertexs;

                SetPolyG3(polyG3);
                if(color) {
                    setRGB0(polyG3, color->r, color->g, color->b);
                    setRGB1(polyG3, color->r, color->g, color->b);
                    setRGB2(polyG3, color->r, color->g, color->b);
                }
                else {
                    setRGB0(polyG3, vertexs[i].color.r,   vertexs[i].color.g,   vertexs[i].color.b);
                    setRGB1(polyG3, vertexs[i+1].color.r, vertexs[i+1].color.g, vertexs[i+1].color.b);
                    setRGB2(polyG3, vertexs[i+2].color.r, vertexs[i+2].color.g, vertexs[i+2].color.b);
                }

                nclip = RotAverageNclip3(&vertexs[index0].position, &vertexs[index1].position, &vertexs[index2].position,
                                        (long *)&polyG3->x0, (long *)&polyG3->x1, (long *)&polyG3->x2, &p, &otz, &flg);
                if (nclip <= 0) continue;
                if ((otz <= 0) || (otz >= orderingTableCount)) continue;

				addPrim(&orderingTable[otz], polyG3);
                _dcRender_IncPrimitive(render, sizeof(POLY_G3));
            }
            break;
            case POLIGON_VERTEX_TEXTURED:
            {
                POLY_FT3* polyFT3 = (POLY_FT3*)poly;
                SDC_VertexTextured *vertexs = (SDC_VertexTextured *)mesh->vertexs;

                SetPolyFT3(polyFT3);
                setRGB0(polyFT3, curr_color.r, curr_color.g, curr_color.b);
                setUV3(polyFT3, vertexs[i].u , vertexs[i].v, vertexs[i+1].u , vertexs[i+1].v, vertexs[i+2].u , vertexs[i+2].v);

                nclip = RotAverageNclip3(&vertexs[index0].position, &vertexs[index1].position, &vertexs[index2].position,
                                        (long *)&polyFT3->x0, (long *)&polyFT3->x1, (long *)&polyFT3->x2, &p, &otz, &flg);
                if (nclip <= 0) continue;
                if ((otz <= 0) || (otz >= orderingTableCount)) continue;

				addPrim(&orderingTable[otz], polyFT3);

                _dcRender_IncPrimitive(render, sizeof(POLY_FT3));
            }
            break;
            case POLIGON_VERTEX_TEXTURED_GSHADED:
            {
                POLY_GT3* polyGT3 = (POLY_GT3*)poly;
                SDC_VertexTextured *vertexs = (SDC_VertexTextured *)mesh->vertexs;
                SetPolyGT3(polyGT3);

                setRGB0(polyGT3, curr_color.r, curr_color.g, curr_color.b);
                setRGB1(polyGT3, curr_color.r, curr_color.g, curr_color.b);
                setRGB2(polyGT3, curr_color.r, curr_color.g, curr_color.b);

                setUV3(polyGT3, vertexs[i].u , vertexs[i].v, vertexs[i+1].u , vertexs[i+1].v, vertexs[i+2].u , vertexs[i+2].v);

                nclip = RotAverageNclip3(&vertexs[index0].position, &vertexs[index1].position, &vertexs[index2].position,
                                        (long *)&polyGT3->x0, (long *)&polyGT3->x1, (long *)&polyGT3->x2, &p, &otz, &flg);
                if (nclip <= 0) continue;
                if ((otz <= 0) || (otz >= orderingTableCount)) continue;

				addPrim(&orderingTable[otz], polyGT3);
                _dcRender_IncPrimitive(render, sizeof(POLY_GT3));
            }
            break;
        }
    }
}