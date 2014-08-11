#ifndef TILEDIMAGE_H
#define TILEDIMAGE_H

#include "tilemap.h"
#include "tilecache.h"

#include <cstdint>
#include <OpenImageIO/imagebuf.h>

class TiledImage
{
    // data members
public:
    TileMap tileMap; //< tile map
    TileCache tileCache; //< tile cache

    uint32_t pw; //< pixel width
    uint32_t ph; //< pixel height
    uint32_t tw; //< tile width
    uint32_t th; //< tile hight
    uint32_t mw; //< map width
    uint32_t mh; //< map height

    //constructors
    TiledImage( ); //< default constructor
    TiledImage( uint32_t w, uint32_t h, uint32_t tw = 32, uint32_t th = 32 );

    // modifications
    void setSize( uint32_t w, uint32_t h );
    void setTileSize( uint32_t w, uint32_t h );
    void setTileMap( TileMap tileMap );

    void mapFromCSV( std::string );

    /// generation
    void squareFromCache();

    // data access
    OpenImageIO::ImageBuf *getRegion(
            uint32_t x1 = 0, uint32_t y1 = 0,
            uint32_t x2 = 0, uint32_t y2 = 0 );
};

#endif //TILEDIMAGE_H
