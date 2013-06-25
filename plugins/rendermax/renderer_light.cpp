#include "renderer_light.hpp"

#include <functional>
#include <string>
#include <math.h>


#include "LuaTools.h"

#include "modules/Gui.h"
#include "modules/Screen.h"
#include "modules/Maps.h"

#include "modules/Units.h"

#include "df/graphic.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/viewscreen_dungeonmodest.h"
#include "df/flow_info.h"
#include "df/world.h"
#include "df/building.h"
#include "df/building_doorst.h"
#include "df/plant.h"
#include "df/plant_raw.h"

using df::global::gps;
using namespace DFHack;
using df::coord2d;


const float RootTwo = 1.4142135623730950488016887242097f;

rect2d getMapViewport()
{
    const int AREA_MAP_WIDTH = 23;
    const int MENU_WIDTH = 30;
    if(!gps || !df::viewscreen_dwarfmodest::_identity.is_instance(DFHack::Gui::getCurViewscreen()))
    {
        if(gps && df::viewscreen_dungeonmodest::_identity.is_instance(DFHack::Gui::getCurViewscreen()))
        {
            return mkrect_wh(0,0,gps->dimx,gps->dimy);
        }
        else
            return mkrect_wh(0,0,0,0);
        
    }
    int w=gps->dimx;
    int h=gps->dimy;
    int view_height=h-2;
    int area_x2 = w-AREA_MAP_WIDTH-2;
    int menu_x2=w-MENU_WIDTH-2;
    int menu_x1=area_x2-MENU_WIDTH-1;
    int view_rb=w-1;
    
    int area_pos=*df::global::ui_area_map_width;
    int menu_pos=*df::global::ui_menu_width;
    if(area_pos<3)
    {
        view_rb=area_x2;
    }
    if (menu_pos<area_pos || df::global::ui->main.mode!=0)
    {
        if (menu_pos >= area_pos) 
            menu_pos = area_pos-1;
        int menu_x = menu_x2;
        if(menu_pos < 2) menu_x = menu_x1;
        view_rb = menu_x;
    }
    return mkrect_wh(1,1,view_rb,view_height+1);
}
lightingEngineViewscreen::lightingEngineViewscreen(renderer_light* target):lightingEngine(target)
{
    reinit();
    defaultSettings();
    loadSettings();
}

void lightingEngineViewscreen::reinit()
{
    if(!gps)
        return;
    w=gps->dimx;
    h=gps->dimy;
    size_t size=w*h;
    lightMap.resize(size,lightCell(1,1,1));
    ocupancy.resize(size);
    lights.resize(size);
}
void plotCircle(int xm, int ym, int r,std::function<void(int,int)> setPixel)
{
    int x = -r, y = 0, err = 2-2*r; /* II. Quadrant */ 
    do {
        setPixel(xm-x, ym+y); /*   I. Quadrant */
        setPixel(xm-y, ym-x); /*  II. Quadrant */
        setPixel(xm+x, ym-y); /* III. Quadrant */
        setPixel(xm+y, ym+x); /*  IV. Quadrant */
        r = err;
        if (r <= y) err += ++y*2+1;           /* e_xy+e_y < 0 */
        if (r > x || err > y) err += ++x*2+1; /* e_xy+e_x > 0 or no 2nd y-step */
    } while (x < 0);
}
void plotLine(int x0, int y0, int x1, int y1,std::function<bool(int,int,int,int)> setPixel)
{
    int dx =  abs(x1-x0), sx = x0<x1 ? 1 : -1;
    int dy = -abs(y1-y0), sy = y0<y1 ? 1 : -1; 
    int err = dx+dy, e2; /* error value e_xy */
    int rdx=0;
    int rdy=0;
    for(;;){  /* loop */
        if(rdx!=0 || rdy!=0) //dirty hack to skip occlusion on the first tile.
            if(!setPixel(rdx,rdy,x0,y0))
                return;
        if (x0==x1 && y0==y1) break;
        e2 = 2*err;
        rdx=rdy=0;
        if (e2 >= dy) { err += dy; x0 += sx; rdx=sx;} /* e_xy+e_x > 0 */
        if (e2 <= dx) { err += dx; y0 += sy; rdy=sy;} /* e_xy+e_y < 0 */
    }
}
lightCell blend(lightCell a,lightCell b)
{
    return lightCell(std::max(a.r,b.r),std::max(a.g,b.g),std::max(a.b,b.b));
}
bool lightingEngineViewscreen::lightUpCell(lightCell& power,int dx,int dy,int tx,int ty)
{
    if(isInViewport(coord2d(tx,ty),mapPort))
    {
        size_t tile=getIndex(tx,ty);
        int dsq=dx*dx+dy*dy;
        float dt=1;
        if(dsq == 1)
            dt=1;
        else if(dsq == 2)
            dt = RootTwo;
        else if(dsq == 0)
            dt = 0;
        else
            dt=sqrt((float)dsq);
        lightCell& v=ocupancy[tile];
        lightSource& ls=lights[tile];
        bool wallhack=false;
        if(v.r+v.g+v.b==0)
            wallhack=true;
        
        if (dsq>0 && !wallhack)
        {
            power*=v.power(dt);
        }
        if(ls.radius>0 && dsq>0)
        {
            if(power<ls.power)
                return false;
        }
        //float dt=sqrt(dsq);
        lightCell oldCol=lightMap[tile];
        lightCell ncol=blend(power,oldCol);
        lightMap[tile]=ncol;
        
        if(wallhack)
            return false;
        float pwsq=power.r*power.r+power.g*power.g+power.b*power.b;
        return pwsq>levelDim*levelDim;
    }
    else
        return false;
}
void lightingEngineViewscreen::doRay(lightCell power,int cx,int cy,int tx,int ty)
{
    using namespace std::placeholders;
    lightCell curPower=power;
    plotLine(cx,cy,tx,ty,std::bind(&lightingEngineViewscreen::lightUpCell,this,std::ref(curPower),_1,_2,_3,_4));
}
void lightingEngineViewscreen::doFovs()
{
    mapPort=getMapViewport();
    using namespace std::placeholders;

    for(int i=mapPort.first.x;i<mapPort.second.x;i++)
        for(int j=mapPort.first.y;j<mapPort.second.y;j++)
        {
            lightSource& csource=lights[getIndex(i,j)];
            if(csource.radius>0)
            {
                lightCell power=csource.power;
                int radius =csource.radius;
                if(csource.flicker)
                {
                    float flicker=(rand()/(float)RAND_MAX)/2.0f+0.5f;
                    radius*=flicker;
                    power=power*flicker;
                }
                int surrounds = 0;
                lightCell curPower;

                lightUpCell(curPower = power, 0, 0,i+0, j+0);
                {
                    surrounds += lightUpCell(curPower = power, 0, 1,i+0, j+1);
                    surrounds += lightUpCell(curPower = power, 1, 1,i+1, j+1);
                    surrounds += lightUpCell(curPower = power, 1, 0,i+1, j+0);
                    surrounds += lightUpCell(curPower = power, 1,-1,i+1, j-1);
                    surrounds += lightUpCell(curPower = power, 0,-1,i+0, j-1);
                    surrounds += lightUpCell(curPower = power,-1,-1,i-1, j-1);
                    surrounds += lightUpCell(curPower = power,-1, 0,i-1, j+0);
                    surrounds += lightUpCell(curPower = power,-1, 1,i-1, j+1);
                }
                if(surrounds)
                    plotCircle(i,j,radius,
                    std::bind(&lightingEngineViewscreen::doRay,this,power,i,j,_1,_2));
            }
        }
}
void lightingEngineViewscreen::calculate()
{
    rect2d vp=getMapViewport();
    const lightCell dim(levelDim,levelDim,levelDim);
    lightMap.assign(lightMap.size(),lightCell(1,1,1));
    lights.assign(lights.size(),lightSource());
    for(int i=vp.first.x;i<vp.second.x;i++)
    for(int j=vp.first.y;j<vp.second.y;j++)
    {
        lightMap[getIndex(i,j)]=dim;
    }
    doOcupancyAndLights();
    doFovs();
    //for each lightsource in viewscreen+x do light
}
void lightingEngineViewscreen::updateWindow()
{
    tthread::lock_guard<tthread::fast_mutex> guard(myRenderer->dataMutex);
    if(lightMap.size()!=myRenderer->lightGrid.size())
    {
        reinit();
        myRenderer->invalidate();
        return;
    }
    
    //if(showOcupancy)
    //std::swap(ocupancy,myRenderer->lightGrid);
    //else
    std::swap(lightMap,myRenderer->lightGrid);
    rect2d vp=getMapViewport();
    
    //myRenderer->invalidateRect(vp.first.x,vp.first.y,vp.second.x-vp.first.x,vp.second.y-vp.first.y);
    myRenderer->invalidate();
    //std::copy(lightMap.begin(),lightMap.end(),myRenderer->lightGrid.begin());
}
void lightSource::combine(const lightSource& other)
{
    power=blend(power,other.power);
    radius=std::max(other.radius,radius);//hack... but who cares
}
bool lightingEngineViewscreen::addLight(int tileId,const lightSource& light)
{
    bool wasLight=lights[tileId].radius>0;
    lights[tileId].combine(light);
    if(light.flicker)
        lights[tileId].flicker=true;
    return wasLight;
}
lightCell getStandartColor(int colorId)
{
    return lightCell(df::global::enabler->ccolor[colorId][0]/255.0f,
        df::global::enabler->ccolor[colorId][1]/255.0f,
        df::global::enabler->ccolor[colorId][2]/255.0f);
}
int getPlantNumber(const std::string& id)
{
    std::vector<df::plant_raw*>& vec=df::plant_raw::get_vector();
    for(int i=0;i<vec.size();i++)
    {
        if(vec[i]->id==id)
            return i;
    }
    return -1;
}
void addPlant(const std::string& id,std::map<int,lightSource>& map,const lightSource& v)
{
    int nId=getPlantNumber(id);
    if(nId>0)
    {
        map[nId]=v;
    }
}
matLightDef* lightingEngineViewscreen::getMaterial(int matType,int matIndex)
{
    auto it=matDefs.find(std::make_pair(matType,matIndex));
    if(it!=matDefs.end())
        return &it->second;
    else 
        return NULL;
}
void lightingEngineViewscreen::applyMaterial(int tileId,const matLightDef& mat,float size, float thickness)
{
    if(mat.isTransparent)
    {
        if(thickness > 0.999 && thickness < 1.001)
            ocupancy[tileId]*=mat.transparency;
        else
            ocupancy[tileId]*=(mat.transparency.power(thickness));
    }
    else
        ocupancy[tileId]=lightCell(0,0,0);
    if(mat.isEmiting)
        addLight(tileId,mat.makeSource(size));
}
bool lightingEngineViewscreen::applyMaterial(int tileId,int matType,int matIndex,float size,const matLightDef* def)
{
    matLightDef* m=getMaterial(matType,matIndex);
    if(m)
    {
        applyMaterial(tileId,*m,size);
        return true;
    }
    else if(def)
    {
        applyMaterial(tileId,*def,size);
    }
    return false;
}
lightCell lightingEngineViewscreen::propogateSun(MapExtras::Block* b, int x,int y,const lightCell& in,bool lastLevel)
{
    const lightCell matStairCase(0.9f,0.9f,0.9f);
    lightCell ret=in;
    coord2d innerCoord(x,y);
    df::tiletype type = b->tiletypeAt(innerCoord);
    df::tile_designation d = b->DesignationAt(innerCoord);
    //df::tile_occupancy o = b->OccupancyAt(innerCoord);
    df::tiletype_shape shape = ENUM_ATTR(tiletype,shape,type);
    df::tiletype_shape_basic basic_shape = ENUM_ATTR(tiletype_shape, basic_shape, shape);
    DFHack::t_matpair mat=b->staticMaterialAt(innerCoord);
    df::tiletype_material tileMat= ENUM_ATTR(tiletype,material,type);

    matLightDef* lightDef;
    if(tileMat==df::tiletype_material::FROZEN_LIQUID)
        lightDef=&matIce;
    else
        lightDef=getMaterial(mat.mat_type,mat.mat_index);
    
    if(!lightDef || !lightDef->isTransparent)
        lightDef=&matWall;
    if(basic_shape==df::tiletype_shape_basic::Wall)
    {
        ret*=lightDef->transparency;
    }
    else if(basic_shape==df::tiletype_shape_basic::Floor || basic_shape==df::tiletype_shape_basic::Ramp || shape==df::tiletype_shape::STAIR_UP)
    {
        if(!lastLevel)
            ret*=lightDef->transparency.power(1.0f/7.0f); 
    }
    else if(shape==df::tiletype_shape::STAIR_DOWN || shape==df::tiletype_shape::STAIR_UPDOWN)
    {
        ret*=matStairCase;
    }
    if(d.bits.liquid_type == df::enums::tile_liquid::Water && d.bits.flow_size > 0)
    {
        ret *=matWater.transparency.power((float)d.bits.flow_size/7.0f);
    }
    else if(d.bits.liquid_type == df::enums::tile_liquid::Magma && d.bits.flow_size > 0)
    {
        ret *=matLava.transparency.power((float)d.bits.flow_size/7.0f);
    }
    return ret;
}
coord2d lightingEngineViewscreen::worldToViewportCoord(const coord2d& in,const rect2d& r,const coord2d& window2d)
{
    return in-window2d+r.first;
}
bool lightingEngineViewscreen::isInViewport(const coord2d& in,const rect2d& r)
{
    if(in.x>=r.first.x && in.y>=r.first.y && in.x<r.second.x && in.y<r.second.y)
        return true;
    return false;
}
static size_t max_list_size = 100000; // Avoid iterating over huge lists
void lightingEngineViewscreen::doSun(const lightSource& sky,MapExtras::MapCache& map)
{
    //TODO fix this mess
    int window_x=*df::global::window_x;
    int window_y=*df::global::window_y;
    coord2d window2d(window_x,window_y);
    int window_z=*df::global::window_z;
    rect2d vp=getMapViewport();
    coord2d vpSize=rect_size(vp);
    rect2d blockVp;
    blockVp.first=window2d/16;
    blockVp.second=(window2d+vpSize)/16;
    blockVp.second.x=std::min(blockVp.second.x,(int16_t)df::global::world->map.x_count_block);
    blockVp.second.y=std::min(blockVp.second.y,(int16_t)df::global::world->map.y_count_block);
    //endof mess
    for(int blockX=blockVp.first.x;blockX<=blockVp.second.x;blockX++)
    for(int blockY=blockVp.first.y;blockY<=blockVp.second.y;blockY++)
    {
        lightCell cellArray[16][16];
        for(int block_x = 0; block_x < 16; block_x++)
        for(int block_y = 0; block_y < 16; block_y++)
            cellArray[block_x][block_y] = sky.power;

        int emptyCell=0;
        for(int z=df::global::world->map.z_count-1;z>=window_z && emptyCell<256;z--)
        {
            MapExtras::Block* b=map.BlockAt(DFCoord(blockX,blockY,z));
            if(!b)
                continue;
            emptyCell=0;
            for(int block_x = 0; block_x < 16; block_x++)
            for(int block_y = 0; block_y < 16; block_y++)
            {
                lightCell& curCell=cellArray[block_x][block_y];
                curCell=propogateSun(b,block_x,block_y,curCell,z==window_z);
                if(curCell.dot(curCell)<0.003f)
                    emptyCell++;                
            }
        }
        if(emptyCell==256)
            continue;
        for(int block_x = 0; block_x < 16; block_x++)
        for(int block_y = 0; block_y < 16; block_y++)
        {
            lightCell& curCell=cellArray[block_x][block_y];
            df::coord2d pos;
            pos.x = blockX*16+block_x;
            pos.y = blockY*16+block_y;
            pos=worldToViewportCoord(pos,vp,window2d);
            if(isInViewport(pos,vp) && curCell.dot(curCell)>0.003f)
            {
                lightSource sun=lightSource(curCell,15);
                addLight(getIndex(pos.x,pos.y),sun);
            }
        }
    }
}
void lightingEngineViewscreen::doOcupancyAndLights()
{
    // TODO better curve (+red dawn ?)
    float daycol = 1;//abs((*df::global::cur_year_tick % 1200) - 600.0) / 400.0;
    lightCell sky_col(daycol, daycol, daycol);
    lightSource sky(sky_col, 15);

    lightSource candle(lightCell(0.96f,0.84f,0.03f),5);
    lightSource torch(lightCell(0.9f,0.75f,0.3f),8);

    //perfectly blocking material
    
    MapExtras::MapCache cache;
    doSun(sky,cache);

    int window_x=*df::global::window_x;
    int window_y=*df::global::window_y;
    coord2d window2d(window_x,window_y);
    int window_z=*df::global::window_z;
    rect2d vp=getMapViewport();
    coord2d vpSize=rect_size(vp);
    rect2d blockVp;
    blockVp.first=coord2d(window_x,window_y)/16;
    blockVp.second=(window2d+vpSize)/16;
    blockVp.second.x=std::min(blockVp.second.x,(int16_t)df::global::world->map.x_count_block);
    blockVp.second.y=std::min(blockVp.second.y,(int16_t)df::global::world->map.y_count_block);
   
    for(int blockX=blockVp.first.x;blockX<=blockVp.second.x;blockX++)
    for(int blockY=blockVp.first.y;blockY<=blockVp.second.y;blockY++)
    {
        MapExtras::Block* b=cache.BlockAt(DFCoord(blockX,blockY,window_z));
        MapExtras::Block* bDown=cache.BlockAt(DFCoord(blockX,blockY,window_z-1));
        if(!b)
            continue; //empty blocks fixed by sun propagation

        for(int block_x = 0; block_x < 16; block_x++)
        for(int block_y = 0; block_y < 16; block_y++)
        {
            df::coord2d pos;
            pos.x = blockX*16+block_x;
            pos.y = blockY*16+block_y;
            df::coord2d gpos=pos;
            pos=worldToViewportCoord(pos,vp,window2d);
            if(!isInViewport(pos,vp))
                continue;
            int tile=getIndex(pos.x,pos.y);
            lightCell& curCell=ocupancy[tile];
            curCell=matAmbience.transparency;
            

            df::tiletype type = b->tiletypeAt(gpos);
            df::tile_designation d = b->DesignationAt(gpos);
            //df::tile_occupancy o = b->OccupancyAt(gpos);
            df::tiletype_shape shape = ENUM_ATTR(tiletype,shape,type);
            df::tiletype_shape_basic basic_shape = ENUM_ATTR(tiletype_shape, basic_shape, shape);
            df::tiletype_material tileMat= ENUM_ATTR(tiletype,material,type);

            DFHack::t_matpair mat=b->staticMaterialAt(gpos);

            matLightDef* lightDef=getMaterial(mat.mat_type,mat.mat_index);
            if(!lightDef || !lightDef->isTransparent)
                lightDef=&matWall;
            if(shape==df::tiletype_shape::BROOK_BED ||  d.bits.hidden )
            {
                curCell=lightCell(0,0,0);
            }
            else if(shape==df::tiletype_shape::WALL)
            {
                if(tileMat==df::tiletype_material::FROZEN_LIQUID)
                    applyMaterial(tile,matIce);
                else
                    applyMaterial(tile,*lightDef);
            }
            else if(!d.bits.liquid_type && d.bits.flow_size>0 )
            {
                applyMaterial(tile,matWater, 1, (float)d.bits.flow_size/7.0f);
            }
            if(d.bits.liquid_type && d.bits.flow_size>0) 
            {
                applyMaterial(tile,matLava);
            }
            else if(shape==df::tiletype_shape::EMPTY || shape==df::tiletype_shape::RAMP_TOP 
                || shape==df::tiletype_shape::STAIR_DOWN || shape==df::tiletype_shape::STAIR_UPDOWN)
            {
                if(bDown)
                {
                   df::tile_designation d2=bDown->DesignationAt(gpos);
                   if(d2.bits.liquid_type && d2.bits.flow_size>0)
                   {
                       applyMaterial(tile,matLava);
                   }
                }
            }
        }
        
        df::map_block* block=b->getRaw();
        if(!block)
            continue;
        //flows
        for(int i=0;i<block->flows.size();i++)
        {
            df::flow_info* f=block->flows[i];
            if(f && f->density>0 && f->type==df::flow_type::Dragonfire || f->type==df::flow_type::Fire)
            {
                df::coord2d pos=f->pos;
                pos=worldToViewportCoord(pos,vp,window2d);
                int tile=getIndex(pos.x,pos.y);
                if(isInViewport(pos,vp))
                {
                    lightCell fireColor;
                    if(f->density>60)
                    {
                        fireColor=lightCell(0.98f,0.91f,0.30f);
                    }
                    else if(f->density>30)
                    {
                        fireColor=lightCell(0.93f,0.16f,0.16f);
                    }
                    else
                    {
                        fireColor=lightCell(0.64f,0.0f,0.0f);
                    }
                    lightSource fire(fireColor,f->density/5);
                    addLight(tile,fire);
                }
            }
        }
        //plants
        for(int i=0;i<block->plants.size();i++)
        {
            df::plant* cPlant=block->plants[i];

            df::coord2d pos=cPlant->pos;
            pos=worldToViewportCoord(pos,vp,window2d);
            int tile=getIndex(pos.x,pos.y);
            if(isInViewport(pos,vp))
            {
                applyMaterial(tile,419,cPlant->material);
            }
        }
    }
    if(df::global::cursor->x>-30000)
    {
        int wx=df::global::cursor->x-window_x+vp.first.x;
        int wy=df::global::cursor->y-window_y+vp.first.y;
        int tile=getIndex(wx,wy);
        applyMaterial(tile,matCursor);
    }
    //citizen only emit light, if defined
    if(matCitizen.isEmiting)
    for (int i=0;i<df::global::world->units.active.size();++i)
    {
        df::unit *u = df::global::world->units.active[i];
        coord2d pos=worldToViewportCoord(coord2d(u->pos.x,u->pos.y),vp,window2d);
        if(u->pos.z==window_z && isInViewport(pos,vp))
        if (DFHack::Units::isCitizen(u) && !u->counters.unconscious)
            addLight(getIndex(pos.x,pos.y),matCitizen.makeSource());
    }
    //buildings
    for(size_t i = 0; i < df::global::world->buildings.all.size(); i++)
    {
        df::building *bld = df::global::world->buildings.all[i];
        if(window_z!=bld->z)
            continue;
        df::coord2d p1(bld->x1,bld->y1);
        df::coord2d p2(bld->x2,bld->y2);
        p1=worldToViewportCoord(p1,vp,window2d);
        p2=worldToViewportCoord(p1,vp,window2d);
        if(isInViewport(p1,vp)||isInViewport(p2,vp))
        {
            int tile=getIndex(p1.x,p1.y); //TODO multitile buildings. How they would work?
            df::building_type type = bld->getType();

            if (type == df::enums::building_type::WindowGlass || type==df::enums::building_type::WindowGem)
            {
                applyMaterial(tile,bld->mat_type,bld->mat_index);
            }
            if (type == df::enums::building_type::Table)
            {
                addLight(tile,candle);
            }
            if (type==df::enums::building_type::Statue)
            {
                addLight(tile,torch);
            }
            if(type==df::enums::building_type::Door)
            {
                df::building_doorst* door=static_cast<df::building_doorst*>(bld);
                if(door->door_flags.bits.closed)
                    applyMaterial(tile,bld->mat_type,bld->mat_index,1,&matWall);
            }
        }
    }
}
lightCell lua_parseLightCell(lua_State* L)
{
    lightCell ret;

    lua_pushnumber(L,1);
    lua_gettable(L,-2);
    ret.r=lua_tonumber(L,-1);
    lua_pop(L,1);

    lua_pushnumber(L,2);
    lua_gettable(L,-2);
    ret.g=lua_tonumber(L,-1);
    lua_pop(L,1);

    lua_pushnumber(L,3);
    lua_gettable(L,-2);
    ret.b=lua_tonumber(L,-1);
    lua_pop(L,1);

    //Lua::GetOutput(L)->print("got cell(%f,%f,%f)\n",ret.r,ret.g,ret.b);
    return ret;
}
matLightDef lua_parseMatDef(lua_State* L)
{
    
    matLightDef ret;
    lua_getfield(L,-1,"tr");
    if(ret.isTransparent=!lua_isnil(L,-1))
    {
        ret.transparency=lua_parseLightCell(L);
    }
    lua_pop(L,1);

    lua_getfield(L,-1,"em");
    if(ret.isEmiting=!lua_isnil(L,-1))
    {
        ret.emitColor=lua_parseLightCell(L);
        lua_pop(L,1);
        lua_getfield(L,-1,"rad");
        if(lua_isnil(L,-1))
        {
            lua_pop(L,1);
            luaL_error(L,"Material has emittance but no radius");
        }
        else
            ret.radius=lua_tonumber(L,-1);
        lua_pop(L,1);
    }
    else
        lua_pop(L,1);
    //todo flags
    return ret;
}
int lightingEngineViewscreen::parseMaterials(lua_State* L)
{
    auto engine= (lightingEngineViewscreen*)lua_touserdata(L, 1);
    engine->matDefs.clear();
    //color_ostream* os=Lua::GetOutput(L);
    Lua::StackUnwinder unwinder(L);
    lua_getfield(L,2,"materials");
    if(!lua_istable(L,-1))
    {
        luaL_error(L,"Materials table not found.");
        return 0;
    }
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        int type=lua_tonumber(L,-2);
        //os->print("Processing type:%d\n",type);
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            int index=lua_tonumber(L,-2);
            //os->print("\tProcessing index:%d\n",index);
            engine->matDefs[std::make_pair(type,index)]=lua_parseMatDef(L);
            
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    return 0;
}
#define LOAD_SPECIAL(lua_name,class_name) \
    lua_getfield(L,-1,#lua_name);\
    if(!lua_isnil(L,-1))engine->class_name=lua_parseMatDef(L);\
    lua_pop(L,1)
int lightingEngineViewscreen::parseSpecial(lua_State* L)
{
    auto engine= (lightingEngineViewscreen*)lua_touserdata(L, 1);
    Lua::StackUnwinder unwinder(L);
    lua_getfield(L,2,"special");
    if(!lua_istable(L,-1))
    {
        luaL_error(L,"Special table not found.");
        return 0;
    }
    LOAD_SPECIAL(LAVA,matLava);
    LOAD_SPECIAL(WATER,matWater);
    LOAD_SPECIAL(FROZEN_LIQUID,matIce);
    LOAD_SPECIAL(AMBIENT,matAmbience);
    LOAD_SPECIAL(CURSOR,matCursor);
    LOAD_SPECIAL(CITIZEN,matCitizen);
    lua_getfield(L,-1,"LevelDim");
    if(!lua_isnil(L,-1) && lua_isnumber(L,-1))engine->levelDim=lua_tonumber(L,-1);
    lua_pop(L,1);
    return 0;
}
#undef LOAD_SPECIAL
void lightingEngineViewscreen::defaultSettings()
{
    matAmbience=matLightDef(lightCell(0.85f,0.85f,0.85f));
    matLava=matLightDef(lightCell(0.8f,0.2f,0.2f),lightCell(0.8f,0.2f,0.2f),5);
    matWater=matLightDef(lightCell(0.6f,0.6f,0.8f));
    matIce=matLightDef(lightCell(0.7f,0.7f,0.9f));
    matCursor=matLightDef(lightCell(0.96f,0.84f,0.03f),11);
    matCursor.flicker=true;
    matWall=matLightDef(lightCell(0,0,0));
    matCitizen=matLightDef(lightCell(0.8f,0.8f,0.9f),6);
    levelDim=0.2f;
}
void lightingEngineViewscreen::loadSettings()
{
    
    const std::string settingsfile="rendermax.lua";
    CoreSuspender lock;
    color_ostream_proxy out(Core::getInstance().getConsole());
    
    lua_State* s=DFHack::Lua::Core::State;
    lua_newtable(s);
    int env=lua_gettop(s);
    try{
        int ret=luaL_loadfile(s,settingsfile.c_str());
        if(ret==LUA_ERRFILE)
        {
            out.printerr("File not found:%s\n",settingsfile.c_str());
            lua_pop(s,1);
        }
        else if(ret==LUA_ERRSYNTAX)
        {
            out.printerr("Syntax error:\n\t%s\n",lua_tostring(s,-1));
        }
        else
        {
            lua_pushvalue(s,env);
            
            if(Lua::SafeCall(out,s,1,0))
            {
                lua_pushcfunction(s, parseMaterials);
                lua_pushlightuserdata(s, this);
                lua_pushvalue(s,env);
                Lua::SafeCall(out,s,2,0);
                out.print("%d materials loaded\n",matDefs.size());

                lua_pushcfunction(s, parseSpecial);
                lua_pushlightuserdata(s, this);
                lua_pushvalue(s,env);
                Lua::SafeCall(out,s,2,0);
                
            }
            
        }
    }
    catch(std::exception& e)
    {
        out.printerr("%s",e.what());
    }
    lua_pop(s,1);
}
