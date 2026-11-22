#include "stdafx.h"
#include "PrototypeScene.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace prototype {
namespace {
Renderer* draw = nullptr;
const echo::GameState* game = nullptr;
const View* view = nullptr;
constexpr double Pi = 3.141592653589793;
Color C(unsigned rgb, float alpha = 1) {
    return Color(((rgb>>16)&255)/255.f, ((rgb>>8)&255)/255.f, (rgb&255)/255.f, alpha);
}
Color Tint(Color c, float value) {
    return Color(c.r*value,c.g*value,c.b*value,c.a).Emissive(c.emission);
}
uint32_t Hash(int x,int y,int salt=0) {
    uint32_t v=uint32_t(x)*374761393u+uint32_t(y)*668265263u+uint32_t(salt)*2246822519u;
    v=(v^(v>>13))*1274126177u; return v^(v>>16);
}
int Mod(int n,int m) { return (n%m+m)%m; }
constexpr int RoadSpacing=6, BuildingSpacing=4, LotTileRadius=1;
constexpr double BuildingWidth=1.85, BuildingDepth=1.65;
constexpr double CafeX=2.0, CafeY=-3.0;
constexpr double CafeWidth=2.6, CafeDepth=1.5;
constexpr double CafeRoofWidth=2.8, CafeRoofDepth=1.7;
constexpr double LotSize=2*LotTileRadius+1;
constexpr double PlayerClearance=.12;
static_assert(BuildingWidth<LotSize && BuildingDepth<LotSize, "Building must fit inside its lot");
static_assert(CafeRoofWidth<LotSize && CafeRoofDepth<LotSize, "Cafe roof must fit inside its lot");

bool PlazaTile(int x,int y) { return std::abs(x)<3 && std::abs(y)<3; }
bool RoadGridTile(int x,int y) { return Mod(x,RoadSpacing)==0 || Mod(y,RoadSpacing)==0; }
bool CityRoadTile(int x,int y) { return !PlazaTile(x,y) && RoadGridTile(x,y); }
// Preserve the original decoration anchors for Lumia, independently of city lots.
bool DecorationAnchor(int x,int y) {
    return !(std::abs(x)<5 && std::abs(y)<5)
        && Mod(x,BuildingSpacing)==0 && Mod(y,BuildingSpacing)==0;
}
bool LotAvoidsRoad(double x,double y) {
    // Tiles are centered on integer coordinates, with boundaries at +/-0.5.
    // Exact boundary contact is allowed; any overlap with a road tile is rejected.
    const int firstX=int(std::floor(x-LotSize*.5+.5));
    const int lastX=int(std::ceil(x+LotSize*.5+.5))-1;
    const int firstY=int(std::floor(y-LotSize*.5+.5));
    const int lastY=int(std::ceil(y+LotSize*.5+.5))-1;
    for(int tx=firstX;tx<=lastX;++tx) for(int ty=firstY;ty<=lastY;++ty)
        if(CityRoadTile(tx,ty)) return false;
    return true;
}
bool CityBuildingAt(int x,int y) {
    return DecorationAnchor(x,y) && LotAvoidsRoad(x,y);
}
bool CityLotTile(int x,int y) {
    if(std::abs(x-int(CafeX))<=LotTileRadius && std::abs(y-int(CafeY))<=LotTileRadius)
        return true;
    const int bx=int(std::round(double(x)/BuildingSpacing))*BuildingSpacing;
    const int by=int(std::round(double(y)/BuildingSpacing))*BuildingSpacing;
    return std::abs(x-bx)<=LotTileRadius && std::abs(y-by)<=LotTileRadius && CityBuildingAt(bx,by);
}
Vec2 Iso(double x,double y,float z=0) {
    return Vec2(float(710+(x-view->cameraX-y+view->cameraY)*48),
                float(485+(x-view->cameraX+y-view->cameraY)*24-z));
}
float Distance(double ax,double ay,double bx,double by) { return float(std::hypot(ax-bx,ay-by)); }
void Poly(std::initializer_list<Vec2> points,Color color) { draw->Polygon(std::vector<Vec2>(points),color); }
void Label(float x,float y,const std::wstring& text,float size=16,Color color=C(0xDDE8EE),int weight=400) {
    draw->Text(x,y,text,size,color,weight);
}
void Diamond(double x,double y,float rx,float ry,Color color,float z=0) {
    Vec2 p=Iso(x,y,z);
    Poly({Vec2(p.x,p.y-ry),Vec2(p.x+rx,p.y),Vec2(p.x,p.y+ry),Vec2(p.x-rx,p.y)},color);
}
void Ellipse(float x,float y,float rx,float ry,Color color) {
    std::vector<Vec2> vertices;
    for(int i=0;i<40;++i) { float a=float(i*2*Pi/40); vertices.emplace_back(x+std::cos(a)*rx,y+std::sin(a)*ry); }
    draw->Polygon(vertices,color);
}
void Ring(float x,float y,float rx,float ry,float thickness,Color color) {
    Vec2 previous(x+rx,y);
    for(int i=1;i<=64;++i) {
        float a=float(i*2*Pi/64); Vec2 p(x+std::cos(a)*rx,y+std::sin(a)*ry);
        draw->Line(previous,p,thickness,color); previous=p;
    }
}
void Box(double x,double y,double width,double depth,float height,Color color,float base=0) {
    Vec2 a=Iso(x-width/2,y-depth/2,base), b=Iso(x+width/2,y-depth/2,base);
    Vec2 c=Iso(x+width/2,y+depth/2,base), d=Iso(x-width/2,y+depth/2,base);
    Vec2 at(a.x,a.y-height), bt(b.x,b.y-height), ct(c.x,c.y-height), dt(d.x,d.y-height);
    Poly({d,c,ct,dt},Tint(color,.62f)); Poly({c,b,bt,ct},Tint(color,.84f));
    Poly({at,bt,ct,dt},color);
    draw->Line(dt,ct,1,Tint(color,1.3f)); draw->Line(ct,bt,1,Tint(color,1.3f));
}
void Panel(float x,float y,float w,float h,float opacity=.93f) {
    draw->RoundedRect(x,y+4,w,h,12,C(0x000710,.22f));
    draw->RoundedRect(x,y,w,h,12,C(0x101D2D,opacity));
    draw->Rect(x+14,y,w-28,1,C(0x7699AF,.22f));
}
void KeyCap(float x,float y,const std::wstring& key,const std::wstring& name) {
    draw->RoundedRect(x,y,30,27,5,C(0x23374A));
    Label(x+8,y+3,key,13,C(0xD3E2E9),600); Label(x+39,y+3,name,13,C(0x9FB3C0));
}
void Character(double x,double y,Color coat,bool player,bool spirit=false,int id=0) {
    Vec2 p=Iso(x,y); float bob=spirit?float(std::sin(view->time*2+id)*4-7):0;
    Ellipse(p.x,p.y+3,15,6,C(0x020A14,.5f));
    if(player) Ring(p.x,p.y+3,19,8,2,C(0x83F3DE,.7f).Emissive(.65f));
    if(spirit) {
        coat=coat.Emissive(.65f);
        Ellipse(p.x,p.y-23+bob,28,36,Color(coat.r,coat.g,coat.b,.07f).Emissive(.5f));
        Ring(p.x,p.y,22,9,1.4f,Color(coat.r,coat.g,coat.b,.45f).Emissive(.8f));
    }
    float bottom=p.y+bob;
    draw->Line(Vec2(p.x-5,bottom-18),Vec2(p.x-7,bottom),5,C(0x202D48));
    draw->Line(Vec2(p.x+5,bottom-18),Vec2(p.x+7,bottom),5,C(0x202D48));
    Poly({Vec2(p.x-10,bottom-33),Vec2(p.x+9,bottom-33),Vec2(p.x+13,bottom-9),Vec2(p.x-12,bottom-9)},coat);
    draw->Line(Vec2(p.x-10,bottom-29),Vec2(p.x-15,bottom-14),5,Tint(coat,.8f));
    draw->Line(Vec2(p.x+10,bottom-29),Vec2(p.x+15,bottom-14),5,coat);
    draw->Circle(p.x,bottom-42,10,C(spirit?0xCBFAF2:0xF1C9AF).Emissive(spirit?.45f:0.f));
    Poly({Vec2(p.x-11,bottom-44),Vec2(p.x-8,bottom-53),Vec2(p.x+5,bottom-54),
          Vec2(p.x+12,bottom-44),Vec2(p.x+5,bottom-47),Vec2(p.x-2,bottom-41)},C(player?0xE2D9EF:0x29354A));
    draw->Rect(p.x+4,bottom-44,2,2,C(0x18263C));
    draw->Line(Vec2(p.x-7,bottom-31),Vec2(p.x+7,bottom-31),3,C(player?0xF4B27A:0xC9F9EF));
}
void Building(int x,int y) {
    uint32_t seed=Hash(x,y,5); float height=75+(seed%4)*30.f;
    Color color=C(seed%3==0?0x3D4D6D:(seed%3==1?0x34485B:0x4B4667));
    Box(x,y,BuildingWidth,BuildingDepth,height,color); Box(x,y,1.35,1.2,6,Tint(color,1.17f),height);
    Box(x-.25,y-.15,.5,.5,18,C(0x51647B),height+6);
    Vec2 b=Iso(x+BuildingWidth*.5,y-BuildingDepth*.5);
    Vec2 c=Iso(x+BuildingWidth*.5,y+BuildingDepth*.5), d=Iso(x-BuildingWidth*.5,y+BuildingDepth*.5);
    for(int row=0;row<int(height/25)-1;++row) for(int col=0;col<4;++col) {
        float t=(col+.55f)/4; bool lit=((Hash(x+row,y+col)>>2)%4)!=0;
        Color light=lit?C(seed%2?0xBDA5D2:0xE7BC86,.74f).Emissive(1.1f):C(0x26374C);
        Vec2 q(b.x+(c.x-b.x)*t,b.y+(c.y-b.y)*t-22-row*25);
        Poly({q,Vec2(q.x-8,q.y+4),Vec2(q.x-8,q.y-5),Vec2(q.x,q.y-9)},light);
        Vec2 u(d.x+(c.x-d.x)*t,d.y+(c.y-d.y)*t-22-row*25);
        Poly({u,Vec2(u.x+8,u.y+4),Vec2(u.x+8,u.y-5),Vec2(u.x,u.y-9)},Tint(light,.7f));
    }
}
void Lamp(double x,double y) {
    Vec2 p=Iso(x,y); Ellipse(p.x+5,p.y,39,16,C(0xF4C780,.045f));
    draw->Line(p,Vec2(p.x,p.y-75),3,C(0x435B70));
    draw->Line(Vec2(p.x,p.y-75),Vec2(p.x+15,p.y-75),3,C(0x60748C));
    draw->Circle(p.x+15,p.y-73,11,C(0xFFDDA4,.06f).Emissive(.6f));
    draw->Rect(p.x+8,p.y-75,14,3,C(0xF1D9B0).Emissive(2.4f));
}
void Tree(double x,double y,uint32_t seed) {
    Vec2 p=Iso(x,y); Ellipse(p.x,p.y+2,29,11,C(0x020D16,.34f));
    draw->Line(p,Vec2(p.x,p.y-51),6,C(0x3B4859)); float h=48+float(seed%18);
    Poly({Vec2(p.x,p.y-h-36),Vec2(p.x+31,p.y-31),Vec2(p.x,p.y-19),Vec2(p.x-31,p.y-31)},C(0x285D64));
    Poly({Vec2(p.x,p.y-h-36),Vec2(p.x+31,p.y-31),Vec2(p.x,p.y-19)},C(0x368578));
    draw->Line(Vec2(p.x,p.y-h-30),Vec2(p.x,p.y-27),1,C(0x80C6A2,.3f));
}
void Crystal(double x,double y,float height=65) {
    Vec2 p=Iso(x,y); Ellipse(p.x,p.y,38,17,C(0x76E6D2,.035f));
    Poly({Vec2(p.x,p.y-height),Vec2(p.x+14,p.y-20),Vec2(p.x,p.y+2),Vec2(p.x-14,p.y-20)},C(0x3EBAAC));
    Poly({Vec2(p.x,p.y-height),Vec2(p.x+14,p.y-20),Vec2(p.x,p.y+2)},C(0x81F1D1).Emissive(2.0f));
    draw->Line(Vec2(p.x,p.y-height),Vec2(p.x,p.y+2),1,C(0xE0FFEA).Emissive(2.8f));
    Ring(p.x,p.y+5,25,11,1,C(0x66E0CB,.45f).Emissive(.7f));
}
void Portal() {
    Vec2 p=Iso(-2,.8); Box(-2,.8,1.1,.7,8,C(game->realm?0x437B80:0x536784));
    Ellipse(p.x,p.y-45,24,42,C(0x70DBC9,.12f));
    Ring(p.x,p.y-45,25,42,3,C(0x70DBC9,.8f).Emissive(2.2f));
    Ring(p.x,p.y-45,31,49,1,C(0xC3A5EE,.4f).Emissive(1.0f));
    for(int i=0;i<5;++i) { float a=float(view->time*.6+i*1.3); draw->Circle(p.x+std::cos(a)*26,p.y-45+std::sin(a)*43,2,C(0xD2FFF0).Emissive(2.5f)); }
    Label(p.x-37,p.y+13,game->realm?L"도시로 연결":L"LUMIA 접속",12,C(0xA7E5DD));
}
void Cafe() {
    Box(CafeX,CafeY,CafeWidth,CafeDepth,70,C(0x57627A));
    Box(CafeX,CafeY,CafeRoofWidth,CafeRoofDepth,7,C(0x8E797B),70);
    Vec2 p=Iso(CafeX+CafeWidth*.5,CafeY+CafeDepth*.5,27);
    Poly({Vec2(p.x-62,p.y-30),Vec2(p.x,p.y+1),Vec2(p.x,p.y-23),Vec2(p.x-62,p.y-54)},C(0xC9B6A1));
    Vec2 sign=Iso(CafeX,CafeY,91); draw->RoundedRect(sign.x-63,sign.y-12,126,29,4,C(0x1D2F42));
    Label(sign.x-49,sign.y-9,L"달빛 우체국",17,C(0xF0D2AD),600);
    Vec2 table=Iso(1.0,-1.8); Ellipse(table.x,table.y-15,21,10,C(0xB29580));
    draw->Line(Vec2(table.x,table.y-15),Vec2(table.x,table.y+2),3,C(0x596172));
}
void Planter(double x,double y) {
    Box(x,y,1.25,.6,14,C(0x69798C));
    Box(x,y,1.08,.48,3,C(0x304B4E),14);
    for(int i=0;i<5;++i) {
        Vec2 p=Iso(x-.4+i*.2,y,25);
        Ellipse(p.x,p.y,10,9,C(i%2?0x5C8E7B:0x477866));
    }
    Vec2 light=Iso(x,y+.32,7); draw->Line(Vec2(light.x-23,light.y-11),Vec2(light.x+23,light.y+11),2,C(0xE0C094,.6f));
}
void Bench(double x,double y) {
    Box(x,y,1.1,.35,8,C(0x8E7B75),9);
    Box(x,y-.16,1.1,.08,19,C(0xA19389),17);
    Box(x-.37,y,.08,.3,9,C(0x43556B)); Box(x+.37,y,.08,.3,9,C(0x43556B));
}
void DiscoveryMarker(int cx,int cy) {
    double x=cx*8+2.5,y=cy*8+2.5;
    if(Distance(game->x,game->y,x,y)>11) return;
    Vec2 p=Iso(x,y); bool found=game->HasDiscovery(game->realm,cx,cy);
    Box(x,y,.45,.45,16,C(found?0x47697B:(game->realm?0x547F89:0x87776F)));
    Ring(p.x,p.y-27,12,5,1,C(found?0x9CAEB9:0xE7C68C,.8f));
    draw->Circle(p.x,p.y-31+float(std::sin(view->time*2)*3),3,C(found?0x9CAEB9:0xFFE5AA).Emissive(found?.35f:2.0f));
    if(Distance(game->x,game->y,x,y)<2.4) Label(p.x-68,p.y+17,found?L"발견한 기억 보관함":L"[E] 이름 없는 기록",13,C(0xECD8B0));
}
void World() {
    bool lumia=game->realm==1;
    for(int row=0;row<18;++row) draw->Rect(0,row*50.f,1440,50,C(lumia?(row<6?0x101D30:0x101F2C):(row<6?0x192239:0x17283A)));
    for(int i=0;i<27;++i) {
        float bx=i*62.f-100,by=260+float(Hash(i,0)%70);
        draw->Rect(bx,by-90-float(Hash(i,2)%130),40+float(Hash(i,1)%30),230,C(lumia?0x213047:0x26314A,.33f));
    }
    int centerX=int(std::floor(view->cameraX)),centerY=int(std::floor(view->cameraY)),radius=15;
    for(int sum=-radius*2;sum<=radius*2;++sum) for(int dx=-radius;dx<=radius;++dx) {
        int dy=sum-dx; if(dy<-radius||dy>radius) continue;
        int x=centerX+dx,y=centerY+dy; uint32_t seed=Hash(x,y);
        const bool lot=!lumia && CityLotTile(x,y);
        const bool plaza=PlazaTile(x,y) && !lot;
        const bool road=lumia?RoadGridTile(x,y):CityRoadTile(x,y);
        Color tile=lumia?C(seed%3==0?0x24464B:0x203E44):C(road?0x26364A:(lot?0x465164:0x3A495C));
        if(lumia&&road) tile=C(0x304C59);
        Diamond(x,y,47.3f,23.5f,tile);
        if(plaza) Diamond(x,y,45,22,C(lumia?0x315D61:0x46566A));
        if(road&&Mod(x+y,3)==0&&!plaza) draw->Line(Iso(x-.23,y),Iso(x+.23,y),2,C(lumia?0x7FD5BB:0xB7A68B,.45f));
        if(lumia&&seed%19==0) { Vec2 a=Iso(x,y); draw->Circle(a.x,a.y,2,C(0x8DE2B8,.5f)); }
    }
    Vec2 center=Iso(0,0);
    Ring(center.x,center.y,104,52,1,C(lumia?0x87D5C6:0xD0BFA4,.5f));
    Ring(center.x,center.y,110,55,1,C(lumia?0x87D5C6:0xD0BFA4,.15f));
    struct Object { double depth; int kind; double x,y; int id; };
    std::vector<Object> objects;
    for(int x=centerX-radius;x<=centerX+radius;++x) for(int y=centerY-radius;y<=centerY+radius;++y) {
        if(Distance(game->x,game->y,x,y)>15) continue;
        if(lumia?DecorationAnchor(x,y):CityBuildingAt(x,y))
            objects.push_back({double(x+y),lumia?1:0,double(x),double(y),0});
        if(Mod(x,6)==1&&Mod(y,6)==1&&(std::abs(x)>3||std::abs(y)>3)) objects.push_back({double(x+y),lumia?2:3,double(x),double(y),0});
    }
    if(Distance(game->x,game->y,-2,.8)<18) objects.push_back({-1.2,4,-2,.8,0});
    if(!lumia&&Distance(game->x,game->y,CafeX,CafeY)<18)
        objects.push_back({CafeX+CafeY,5,CafeX,CafeY,0});
    if(!lumia&&Distance(game->x,game->y,1,-1.3)<18) objects.push_back({-.3,6,1,-1.3,0});
    if(!lumia) {
        objects.push_back({-4,10,-3,-1,0}); objects.push_back({5,10,3,2,0});
        objects.push_back({1.75,11,-1.25,3,0}); objects.push_back({-1,11,3,-4,0});
    }
    int cellX=int(std::floor(game->x/8)),cellY=int(std::floor(game->y/8));
    for(int x=cellX-1;x<=cellX+1;++x) for(int y=cellY-1;y<=cellY+1;++y) objects.push_back({double(x*8+y*8+5),9,double(x),double(y),0});
    if(lumia) for(size_t i=0;i<game->echoes.size();++i) {
        const auto& e=game->echoes[i];
        if(Distance(game->x,game->y,e.x,e.y)<16) objects.push_back({e.x+e.y,8,e.x,e.y,int(i)});
    }
    objects.push_back({game->x+game->y,7,game->x,game->y,0});
    std::stable_sort(objects.begin(),objects.end(),[](const Object& a,const Object& b){return a.depth<b.depth;});
    for(const auto& object:objects) {
        Vec2 p=Iso(object.x,object.y);
        switch(object.kind) {
        case 0: Building(int(object.x),int(object.y)); break;
        case 1: Tree(object.x,object.y,Hash(int(object.x),int(object.y))); break;
        case 2: Crystal(object.x,object.y,60+float(Hash(int(object.x),int(object.y))%35)); break;
        case 3: Lamp(object.x,object.y); break;
        case 4: Portal(); break;
        case 5: Cafe(); break;
        case 6:
            Character(object.x,object.y,C(0xAE91BF),false); Label(p.x-13,p.y-78,L"세아",14,C(0xE6D5EE));
            if(Distance(game->x,game->y,1,-1.3)<2) Label(p.x-30,p.y+15,L"[E] 대화",12,C(0xD2C2DF));
            break;
        case 7:
            Character(object.x,object.y,C(0x548EA2),true);
            if(view->attack>0) Ring(p.x+12,p.y-23,55,25,3,C(0xE8D4A9,view->attack*2));
            break;
        case 8: {
            const auto& e=game->echoes[object.id]; Color color=e.protection>=e.aggression?C(0x86DDD0):C(0xDAA1B0);
            Character(object.x,object.y,color,false,true,e.id);
            Label(p.x-27,p.y-91,L"유실체 "+std::to_wstring(e.id),13,color);
            if(Distance(game->x,game->y,e.x,e.y)<2) Label(p.x-25,p.y+18,L"[E] 교감",12,color);
            break;
        }
        case 9: DiscoveryMarker(int(object.x),int(object.y)); break;
        case 10: Planter(object.x,object.y); break;
        case 11: Bench(object.x,object.y); break;
        }
    }
    if(lumia) for(int i=0;i<65;++i) {
        float x=float(Hash(i,4)%1440), y=float(std::fmod(Hash(i,9)%900+view->time*(3+i%5),900));
        draw->Circle(x,y,i%5==0?2.f:1.f,C(0xA8E6D1,.14f+(i%4)*.06f));
    }
}
void Minimap() {
    float x=1164,y=653; Panel(x,y,244,153); Label(x+17,y+13,L"주변의 흔적",13,C(0xAFC2D0),600);
    for(int i=-3;i<=3;++i) for(int j=-3;j<=3;++j) {
        float px=x+122+(i-j)*13,py=y+88+(i+j)*6;
        int wx=int(std::floor(game->x))+i,wy=int(std::floor(game->y))+j;
        const bool road=game->realm==0?CityRoadTile(wx,wy):RoadGridTile(wx,wy);
        const bool lot=game->realm==0 && CityLotTile(wx,wy);
        Poly({Vec2(px,py-5),Vec2(px+11,py),Vec2(px,py+5),Vec2(px-11,py)},
             C(road?0x355267:(lot?0x3B4B60:0x223B4C)));
    }
    auto marker=[&](double wx,double wy,Color c) {
        double dx=wx-game->x,dy=wy-game->y;
        if(std::abs(dx)>3.2||std::abs(dy)>3.2) return;
        draw->Circle(float(x+122+(dx-dy)*13),float(y+88+(dx+dy)*6),3,c);
    };
    marker(-2,.8,C(0xC9B4E2));
    if(game->realm==0) marker(1,-1.3,C(0xD8B48D));
    if(game->realm==1) for(const auto& life:game->echoes) marker(life.x,life.y,C(0x9AEAD8));
    for(const auto& found:game->discoveries) if(found.realm==game->realm)
        marker(found.cellX*8+2.5,found.cellY*8+2.5,C(0xF1D093));
    draw->Circle(x+122,y+88,4,C(0x9AEAD8)); Label(x+17,y+126,L"미지의 구역은 계속 이어집니다",11,C(0x718C9D));
}
void Hud() {
    bool lumia=game->realm==1; Color accent=lumia?C(0x86E4CD):C(0xD9BBA0);
    draw->Rect(0,0,1440,5,accent);
    Label(33,24,L"E C H O",19,C(0x9AB3C8),600); Label(31,53,L"잔향의 법칙",37,C(0xEDF0F0),700);
    Label(34,103,L"존재의 대가  /  PROTOTYPE 01",11,C(0x849BB1));
    Panel(520,25,397,62); draw->Circle(543,53,4,accent);
    Label(557,36,lumia?L"LUMIA  /  가상세계":L"REALITY  /  현실 도시",17,C(0xDCE6E9),600);
    Label(557,63,lumia?L"이름 없는 정원 · 잔향 관측 구역":L"2050  ·  해온시 달빛 지구",10,C(0x8CA7B8));
    Panel(1164,26,244,62); Label(1182,37,lumia?L"[TAB]  현실로 돌아가기":L"[TAB]  루미아로 접속",14,accent,600);
    Label(1182,64,L"[J] 기록   ·   [F1] 도움말",10,C(0x8CA1B5));
    Panel(32,156,250,194); Label(50,172,L"지금, 우리의 이야기",12,accent,600);
    Label(50,200,lumia?L"남겨진 생명의 온도":L"방송이 끝난 뒤에도",20,C(0xE4EBEE),600);
    Label(50,237,lumia?L"당신의 죽음에서 태어난 존재는":L"세아와 이야기를 나누고",13,C(0xA5B8C7));
    Label(50,260,lumia?L"어떤 삶을 살아가게 될까요?":L"화면 너머의 세계를 찾아보세요.",13,C(0xA5B8C7));
    draw->Rect(50,293,212,1,C(0x7890A3,.2f)); draw->Circle(54,319,3,accent);
    Label(66,309,lumia?L"[R] 사망 · 유실체 탄생 체험":L"[E] 대화와 기록 조사",12,C(0xD3DDE0));
    Panel(32,673,250,133); Label(50,688,L"내가 남기는 것",13,C(0xB7C7D1),600);
    Label(50,716,game->ProfileName(),18,accent,600);
    float total=std::max(1.f,game->protection+game->aggression+game->curiosity),start=50;
    Color colors[]={C(0x8DE1C8),C(0xC794AB),C(0xB8B5E9)};
    float values[]={game->protection,game->aggression,game->curiosity};
    for(int i=0;i<3;++i) { float width=212*values[i]/total; draw->Rect(start,753,width,4,colors[i]); start+=width; }
    Label(50,774,L"탄생 "+std::to_wstring(game->echoes.size())+L"   /   발견 "+std::to_wstring(game->discoveries.size()),11,C(0x8DA4B6));
    Panel(1164,156,244,257); draw->Circle(1184,179,3,C(game->broadcasting?0xED938C:0x6D8195));
    Label(1197,168,game->broadcasting?L"LIVE  ·  달빛 탐사 방송":L"방송 대기 중",13,C(0xDDE5E9),600);
    Label(1182,203,game->broadcasting?L"오늘도 같이 걷는 중":L"[B] 방송 시작하기",11,C(0x8EA5B8));
    draw->Rect(1182,232,208,1,C(0x768C9E,.19f));
    Label(1182,249,L"달콩  여기 분위기 너무 좋다",12,C(0xA8C8CB));
    Label(1182,280,lumia?L"새벽별  방금 빛난 거 봤어요?":L"새벽별  세아님 또 길 잃으심?",11,C(0xB9B1D5));
    Label(1182,310,L"세아  ...지름길 찾는 중이거든?",11,C(0xD5BFAF));
    Label(1182,342,game->echoes.empty()?L"익명  저 문, 어제도 있었나요?":L"익명  저 아이, 움직임이 닮았네",11,C(0xA8B9C9));
    Label(1182,383,L"일상도 모험도, 함께라서.",10,C(0x667E90)); Minimap();
    Panel(313,829,815,48); KeyCap(328,840,L"W",L"ASD 이동"); KeyCap(466,840,L"E",L"대화·조사");
    KeyCap(608,840,L"B",L"방송"); KeyCap(722,840,L"Q",L"보호"); KeyCap(837,840,L"R",L"사망 체험"); KeyCap(1000,840,L"J",L"기록");
    Label(34,849,L"한 번의 죽음, 하나의 생명.",12,C(0x839DAC));
    std::wostringstream coords; coords<<int(std::floor(game->x))<<L" : "<<int(std::floor(game->y));
    Label(1178,839,coords.str(),16,C(0xA8B9C5)); Label(1178,864,L"[F5] 저장   [ESC] 닫기",11,C(0x718B9C));
    if(!game->message.empty()&&!view->dialogueOpen&&!view->journalOpen&&!view->helpOpen) {
        Panel(350,741,775,51,.94f); Label(370,755,game->message,13,C(0xCCDFDD));
    }
    if(view->deathFlash>0) draw->Rect(0,0,1440,900,C(0x9BE8DD,view->deathFlash*.18f));
    if(view->dialogueOpen) {
        draw->Rect(0,540,1440,360,C(0x020B15,.36f)); Panel(320,583,800,217,.98f);
        Label(348,603,game->realm?L"이름을 기다리는 생명":L"세아  /  동료 스트리머",15,accent,600);
        std::wistringstream lines(view->dialogue); std::wstring line; float yy=645;
        while(std::getline(lines,line)) { Label(348,yy,line,18,C(0xE1E9EB)); yy+=30; }
        Label(348,762,L"[1] 함께할게   [2] 조금 더 지켜본다   [E / ESC] 대화 마치기",12,C(0x90ADBC));
    }
    if(view->journalOpen||view->helpOpen) {
        draw->Rect(0,0,1440,900,C(0x020813,.58f)); Panel(385,175,670,550,.99f);
        Label(420,207,view->helpOpen?L"첫 번째 여정":L"세계가 기억하는 이야기",28,C(0xE4EEE9),600);
        Label(420,260,view->helpOpen?L"WASD / 방향키   이동     SHIFT   빠르게 걷기":L"사망 기록 "+std::to_wstring(game->deaths)+L"회  ·  태어난 생명 "+std::to_wstring(game->echoes.size()),16,C(0xAEC7CE));
        Label(420,306,view->helpOpen?L"TAB   현실과 루미아 전환":L"현실과 루미아의 발견 기록 "+std::to_wstring(game->discoveries.size())+L"개",16,C(0xAEC7CE));
        Label(420,352,view->helpOpen?L"E   가까운 인물·유실체·기록과 상호작용":L"현재 성향  ·  "+game->ProfileName(),16,C(0xAEC7CE));
        Label(420,398,view->helpOpen?L"Q   보호 행동     SPACE   공격 행동":L"한 번 태어난 생명과 발견한 기록은 저장됩니다.",16,C(0xAEC7CE));
        Label(420,444,view->helpOpen?L"R   루미아에서 사망·탄생을 체험":L"새 지역은 이어지고, 지나온 선택은 남습니다.",16,C(0xAEC7CE));
        Label(420,490,view->helpOpen?L"B   방송 시작·종료     J   기록":L"“네가 가고 싶은 곳부터 가자.”",16,C(0xAEC7CE));
        Label(420,548,view->helpOpen?L"F5   저장     F1   도움말     ESC   닫기·종료":L"지금의 행동이 다음 생명의 성향이 됩니다.",15,C(0xCDBDAA));
        if(view->helpOpen) Label(420,588,L"F2 후처리 전체   F3 블룸   F4 비넷   F6 가장자리 블러",14,C(0xA4C4CB));
        Label(420,640,L"[ESC] 여정으로 돌아가기",14,C(0x7FA99F));
    }
}
}
bool IsBlocked(double x,double y,int realm) {
    if(realm!=0) return false;
    const int bx=int(std::round(x/BuildingSpacing))*BuildingSpacing;
    const int by=int(std::round(y/BuildingSpacing))*BuildingSpacing;
    if(CityBuildingAt(bx,by)
        && std::abs(x-bx)<BuildingWidth*.5+PlayerClearance
        && std::abs(y-by)<BuildingDepth*.5+PlayerClearance) return true;
    return std::abs(x-CafeX)<CafeWidth*.5+PlayerClearance
        && std::abs(y-CafeY)<CafeDepth*.5+PlayerClearance;
}
void Render(Renderer& renderer,const echo::GameState& state,const View& currentView) {
    draw=&renderer; game=&state; view=&currentView;
    World();
    renderer.BeginOverlay();
    Hud();
}
}
