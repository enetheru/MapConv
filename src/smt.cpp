#include "smt.h"

#include <fstream>
#include <sys/time.h>
#include <deque>

#include <OpenImageIO/imageio.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include "nvtt_output_handler.h"
#include "smf.h"
#include "dxt1load.h"

void
SMT::setType(int tileType)
{
    this->tileType = tileType;
    tileSize = 0;
    // Calculate the size of the tile data
    // size is the raw format of dxt1 with 4 mip levels
    // 32x32, 16x16, 8x8, 4x4
    // 512  + 128  + 32 + 8 = 680
    if(tileType == DXT1) {
        int mip = tileRes;
        for( int i=0; i < 4; ++i) {
            tileSize += mip/4 * mip/4 * 8;
            mip /= 2;
        }
    } else {
        if( !quiet )printf("ERROR: '%i' is not a valid compression type", tileType);
        tileSize = -1;
    }
    
    return;
}

bool
SMT::load()
{
    ifstream inFile;
    char magic[16];
    bool status = false;

    // Test File
    if( loadFile.compare( "" ) )
    {
        inFile.open( loadFile.c_str(), ifstream::in );
        if(! inFile.good() )
        {
            if(! quiet )printf( "ERROR: Cannot open '%s'\n", loadFile.c_str() ); 
            status = true;
        } else {
            inFile.read( magic, 16 );
            if( strcmp( magic, "spring tilefile" ) )
            {
                if(! quiet ) printf( "ERROR: %s is not a valid smt\n", loadFile.c_str() );
                status = true;
            }
        }
    } else status = true; // loadFile.compare( "" )

    if( status ) {
        inFile.close();
        return status;
    }

    inFile.seekg(0);
    inFile.read( (char *)&header, sizeof(SMTHeader) );
    inFile.close();

    if( verbose ) {
        cout << "INFO: " << loadFile << endl;
        cout << "\tSMT Version: " << header.version << endl;
        cout << "\tTiles: " << header.nTiles << endl;
        cout << "\tTileRes: " << header.tileRes << endl;
        cout << "\tCompression: ";
        if( header.tileType == DXT1 )cout << "dxt1" << endl;
        else {
            cout << "UNKNOWN" << endl;
            status = true;
        }
    }

    setType( header.tileType );
    return status;
}

bool
SMT::decompile()
{
    cout << loadFile << endl;
    string fileName = this->loadFile.substr(0,this->loadFile.size()-4) + "_tiles.jpg";
    if(verbose )cout << "INFO: Saving to " << fileName << endl;

    ImageBuf *bigBuf = getBig();
    bigBuf->save(fileName);

    return false;
}

class TileBufListEntry {
public:
    ImageBuf image;
    ImageBufAlgo::PixelStats stats;
    int tileNum;
};

bool
SMT::append( ImageBuf &tile )
// Code assumes that tiles are DXT1 compressed at this stage
{
    fstream smtFile( this->saveFile, ios::binary | ios::out | ios::app );
   
    unsigned char *std = (unsigned char *)tile.localpixels();

    // process into dds
    NVTTOutputHandler *nvttHandler = new NVTTOutputHandler( this->tileSize );

    nvtt::InputOptions inputOptions;
    inputOptions.setTextureLayout( nvtt::TextureType_2D, this->tileRes, this->tileRes );
    inputOptions.setMipmapData( std, this->tileRes, this->tileRes );

    nvtt::CompressionOptions compressionOptions;
    compressionOptions.setFormat( nvtt::Format_DXT1a );

    nvtt::OutputOptions outputOptions;
    outputOptions.setOutputHeader( false );
    outputOptions.setOutputHandler( nvttHandler );

    nvtt::Compressor compressor;
    if( this->slow_dxt1 ) compressionOptions.setQuality( nvtt::Quality_Normal ); 
    else           compressionOptions.setQuality( nvtt::Quality_Fastest ); 
    compressor.process( inputOptions, compressionOptions, outputOptions );

    smtFile.write( nvttHandler->buffer, this->tileSize );
    smtFile.close();

    delete nvttHandler;

    this->nTiles += 1;

    // retroactively fix up the tile count.
    smtFile.open( this->saveFile, ios::binary | ios::in | ios::out );
    smtFile.seekp( 20 );
    smtFile.write( (char *)&(this->nTiles), 4 );
    smtFile.close();

    return false;
}

bool
SMT::save2()
{
    if( verbose ) cout << "\nINFO: Creating " << saveFile << endl;

    fstream smtFile( saveFile, ios::binary | ios::out );
    if( !smtFile.good() ) {
        cout << "ERROR: fstream error." << endl;
        return true;
    }

    SMTHeader smtHeader;
    smtHeader.tileRes = tileRes;
    smtHeader.tileType = tileType;

    if( verbose ) {
        cout << "    Version: " << smtHeader.version << endl;
        cout << "    nTiles: n/a\n";
        printf( "    tileRes: (%i,%i)%i.\n", tileRes, tileRes, 4);
        cout << "    tileType: ";
        if( tileType == DXT1 ) cout << "DXT1" << endl;
        cout << "    tileSize: " << tileSize << " bytes" << endl;
    }

    // Writing initial header information.
    smtFile.write( (char *)&smtHeader, sizeof(SMTHeader) );
    smtFile.close();

    // for each source image, load, scale, swizzle, etc.
    ImageBuf imageBuf;
    ImageSpec imageSpec;
    ImageBuf fixBuf; 
    ROI roi( 0, tileRes, 0, tileRes, 0, 1, 0, 4 );
    for( unsigned int i = 0; i < sourceFiles.size(); ++i)
    {
        // Load
        imageBuf.reset( sourceFiles[ i ] );
        imageBuf.read( 0, 0, true, TypeDesc::UINT8 );
        if(! imageBuf.initialized() ) {
            if(! quiet ) printf("ERROR: cannot read: %s\n", sourceFiles[ i ].c_str() );
            continue;
        }
        imageSpec = imageBuf.spec();

        // Scale
        roi.chbegin = 0; roi.chend = 4;
        if( imageSpec.width != roi.xend || imageSpec.height != roi.yend ) {
            if( verbose )
                printf( "WARNING: Image is (%i,%i), wanted (%i, %i),"
                    " Resampling.\n",
                    imageSpec.width, imageSpec.height, roi.xend, roi.yend );
            ImageBufAlgo::resample( fixBuf, imageBuf, false, roi );
            imageBuf.copy( fixBuf );
            fixBuf.clear();
        }

        // Fill the alpha channel
        roi.chend = 3;
        roi.chbegin = 3;
        ImageBufAlgo::add( fixBuf, imageBuf, 1, roi );
        imageBuf.copy(fixBuf);
        fixBuf.clear();

        // Swizzle
        if( verbose )cout << "INFO: Swizzling channels\n";
        int map[] = { 2, 1, 0, 3 };
        ImageBufAlgo::channels( fixBuf, imageBuf, 4, map );
        imageBuf.copy( fixBuf );
        fixBuf.clear();


        append(imageBuf);

        imageBuf.clear();
    }

    return true;
}

bool
SMT::save()
{
    // Make sure we have some source images before continuing
    if( sourceFiles.size() == 0) {
        if( !quiet )cout << "ERROR: No source images to convert" << endl;
        return true;
    }

    // Build SMT Header //
    //////////////////////

    if( verbose ) cout << "\nINFO: Creating " << saveFile << endl;
    fstream smt( saveFile, ios::binary | ios::out );
    if( !smt.good() ) {
        cout << "ERROR: fstream error." << endl;
        return true;
    }

    SMTHeader header;
    header.tileRes = tileRes;
    header.tileType = tileType;
    if( verbose ) {
        cout << "    Version: " << header.version << endl;
        cout << "    nTiles: n/a\n";
        printf( "    tileRes: (%i,%i)%i.\n", tileRes, tileRes, 4);
        cout << "    tileType: ";
        if( tileType == DXT1 ) cout << "DXT1" << endl;
        cout << "    tileSize: " << tileSize << " bytes" << endl;
    }

    smt.write( (char *)&header, sizeof(SMTHeader) );
    smt.close();

    // setup size for index dimensions
    int tcx = width * 16; // tile count x
    int tcz = length * 16; // tile count z
    unsigned int *indexPixels = new unsigned int[tcx * tcz];

    // Load source image
    if( verbose )cout << "INFO: Loading Source Image(s)\n";
    ImageBuf *bigBuf = buildBig();
    ImageSpec bigSpec = bigBuf->spec();

    // Process decals
    if( !decalFile.empty() ) {
        if( verbose )cout << "INFO: Processing decals\n";
        pasteDecals( bigBuf );
    }

    // Swizzle channels
    if( verbose )cout << "INFO: Swizzling channels\n";
    ImageBuf fixBuf;
    int map[] = { 2, 1, 0, 3 };
    ImageBufAlgo::channels( fixBuf, *bigBuf, 4, map );
    bigBuf->copy( fixBuf );
    fixBuf.clear();

    // Process Tiles
    if( verbose )cout << "INFO: Processing tiles\n";

    // Time reporting vars
    timeval t1, t2;
    double elapsedTime;
    deque<double> readings;
    double averageTime = 0;
    double intervalTime = 0;

    // Loop vars
    int totalTiles = tcx * tcz;
    int currentTile;

    // Tile Vars
    ROI roi;
    ImageSpec tileSpec(tileRes, tileRes, 4, TypeDesc::UINT8 );

    // Comparison vars
    bool match;
    unsigned int i;
    string hash;
    vector<string> hashTable;
    TileBufListEntry *listEntry;
    deque<TileBufListEntry *> tileList;

    // Open smt file for writing tiles
    smt.open(saveFile, ios::binary | ios::out | ios::app );

    // loop through tile columns
    for ( int z = 0; z < tcz; z++) {
        // loop through tile rows
        for ( int x = 0; x < tcx; x++) {
            currentTile = z * tcx + x + 1;
            gettimeofday(&t1, NULL);

            // pull a region of the big image to use as a tile.
            roi.xbegin = x * tileRes;
            roi.xend = x * tileRes + tileRes;
            roi.ybegin = z * tileRes;
            roi.yend = z * tileRes + tileRes;
            roi.zbegin = 0;
            roi.zend = 1;
            roi.chbegin = 0;
            roi.chend = 4;

            ImageBuf tempBuf;
            ImageBufAlgo::crop( tempBuf, *bigBuf, roi );
            ImageBuf *tileBuf = new ImageBuf( saveFile, tileSpec, tempBuf.localpixels() );

            // reset match variables
            match = false;
            i = nTiles;

            if( cnum < 0)  {
                // no attempt at reducing tile sizes
                i = nTiles;
            } else if( cnum == 0) {
                // only exact matches will be referenced.
                hash = ImageBufAlgo::computePixelHashSHA1( *tileBuf );
                for( i = 0; i < hashTable.size(); ++i ) {
                    if( !hashTable[i].compare( hash ) ) {
                        match = true;
                        break;
                    } 
                }
                if( !match ) hashTable.push_back( hash );

            } else {
                //Comparison based on numerical differences of pixels
                listEntry = new TileBufListEntry;
                listEntry->image.copy(*tileBuf);
                listEntry->tileNum = nTiles;

                ImageBufAlgo::CompareResults result;
                deque< TileBufListEntry * >::iterator it;

                for(it = tileList.begin(); it != tileList.end(); it++ ) {
                    TileBufListEntry *listEntry2 = *it;
                    ImageBufAlgo::compare( *tileBuf, listEntry2->image,
                            cpet, 1.0f, result);
                    //TODO give control on tweaking matching
                    if((int)result.nfail < cnet) {
                        match = true;
                        i = listEntry2->tileNum;
                        delete listEntry;
                        break;
                    }
                }
                if( !match ) {
                    tileList.push_back(listEntry);
                    if((int)tileList.size() > cnum) {
                        delete tileList[0];
                        tileList.pop_front();
                    }
                }
            }

            // write tile to file.
            if( !match ) {
                append(*tileBuf);
            }

            delete tileBuf;
            // Write index to tilemap
            indexPixels[currentTile-1] = i;

            gettimeofday(&t2, NULL);
            // compute and print the elapsed time in millisec
            elapsedTime = (t2.tv_sec - t1.tv_sec) * 1000.0;      // sec to ms
            elapsedTime += (t2.tv_usec - t1.tv_usec) / 1000.0;   // us to ms
            readings.push_back(elapsedTime);
            if(readings.size() > 1000)readings.pop_front();
            intervalTime += elapsedTime;
            if( verbose && intervalTime > 1 ) {
                for(unsigned int i = 0; i < readings.size(); ++i)
                    averageTime+= readings[i];
                averageTime /= readings.size();
                intervalTime = 0;
                   printf("\033[0G    %i of %i %%%0.1f complete | %%%0.1f savings | %0.1fs remaining.",
                currentTile, totalTiles,
                (float)currentTile / totalTiles * 100,
                (float)(1 - (float)nTiles / (float)currentTile) * 100,
                averageTime * (totalTiles - currentTile) / 1000);
            }
        }
    }
    hashTable.clear();
    if( verbose ) cout << endl;
    smt.close();

    // Save tileindex
    ImageOutput *imageOutput;
    string tilemapFN = saveFile.substr( 0, saveFile.size() - 4 ) + "_tilemap.exr";
    
    imageOutput = ImageOutput::create( tilemapFN );
    if(! imageOutput ) {
        delete [] indexPixels;
        return true;
    }

    ImageSpec tilemapSpec( tcx, tcz, 1, TypeDesc::UINT);
    imageOutput->open( tilemapFN, tilemapSpec );
    imageOutput->write_image( TypeDesc::UINT, indexPixels );
    imageOutput->close();

    delete imageOutput;
    delete [] indexPixels;

    return false;

}

ImageBuf *
SMT::getTile(int tile)
{
    ImageBuf *imageBuf = NULL;
    ImageSpec imageSpec( header.tileRes, header.tileRes, 4, TypeDesc::UINT8 );
    unsigned char *data;

    ifstream smt( loadFile.c_str() );
    if( smt.good() ) {
        unsigned char *temp = new unsigned char[ tileSize ];
        smt.seekg( sizeof(SMTHeader) + tile * tileSize );
        smt.read( (char *)temp, tileSize );
        data = dxt1_load( temp, header.tileRes, header.tileRes );
        delete [] temp;
        char name[11];
        sprintf(name, "tile%6i", tile);
        imageBuf = new ImageBuf( name, imageSpec, data);
    }
    smt.close();
    return imageBuf;    
}

ImageBuf *
SMT::buildBig()
{
    if( verbose && sourceFiles.size() > 1 ) {
        cout << "INFO: Collating source images\n";
        cout << "    nFiles: " << sourceFiles.size() << endl;
        cout << "    stride: " << stride << endl;
        if( sourceFiles.size() % stride != 0 )
            cout << "WARNING: number of source files isnt divisible by stride,"
                " black spots will exist\n";
    }

    // Get values to fix
    ImageBuf *regionBuf = new ImageBuf(sourceFiles[0]);
    regionBuf->read(0,0,false, TypeDesc::UINT8);
    if( !regionBuf->initialized() ) {
        if( !quiet )printf("ERROR: could not build big image\n");
        return NULL;
    }
    ImageSpec regionSpec = regionBuf->spec();
    delete regionBuf;

    // Construct the big buffer
    ImageSpec bigSpec(
        regionSpec.width * stride,
        regionSpec.height * sourceFiles.size() / stride,
        4,
        TypeDesc::UINT8 );
    if( verbose )printf("    Allocating: (%i,%i)%i\n",
        bigSpec.width, bigSpec.height, bigSpec.nchannels );
    ImageBuf *bigBuf = new ImageBuf( "big", bigSpec );

    // Fill the alpha channel
    const float fill[] = { 0, 0, 0, 255 };
    ImageBufAlgo::fill( *bigBuf, fill );

    // Collate the source Files
    int nFiles = sourceFiles.size();
    for( int i = 0; i < nFiles; ++i ) {
        if( verbose )printf( "\033[0G    copying %i of %i, %s",
            i + 1, nFiles, sourceFiles[i].c_str() );
        regionBuf = new ImageBuf( sourceFiles[i] );
        regionBuf->read( 0, 0, false, TypeDesc::UINT8 );
        if( !regionBuf->initialized() ) {
            if( !quiet )printf( "\nERROR: %s could not be loaded.\n",
                sourceFiles[i].c_str() );
            continue;
        }
        regionSpec = regionBuf->spec();

        int x = regionSpec.width * (i % stride);
        int y = regionSpec.height * (i / stride);
        y = bigSpec.height - y - regionSpec.height;

        ImageBufAlgo::paste( *bigBuf, x, y, 0, 0, *regionBuf );    
    }
    if( verbose )cout << endl;

    // rescale constructed big image to wanted size
    ROI roi( 0, width * 512 , 0, length * 512, 0, 1, 0, 4 );
    ImageBuf fixBuf;
    if( bigSpec.width != roi.xend || bigSpec.height != roi.yend ) {
        if( verbose )
            printf( "WARNING: Image is (%i,%i), wanted (%i, %i),"
                " Resampling.\n",
            bigSpec.width, bigSpec.height, roi.xend, roi.yend );
        ImageBufAlgo::resample( fixBuf, *bigBuf, true, roi );
        bigBuf->copy( fixBuf );
    }

    return bigBuf;
}

ImageBuf *
SMT::getBig()
{
    ImageBuf *bigBuf = NULL;
    if(! tilemapFile.compare("") ) {
        bigBuf = collateBig();
    } else {
        bigBuf = reconstructBig();
    }
    return bigBuf;
}

ImageBuf *
SMT::collateBig()
{
    if( verbose )cout << "INFO: Collating Big\n";
    if( !loadFile.compare("") ) {
        if( !quiet )cout << "ERROR: No SMT Loaded." << endl;
        return NULL;
    }
    // OpenImageIO
    ImageBuf *tileBuf = NULL;

    // Allocate Image size large enough to accomodate the tiles,
    int collateStride = ceil(sqrt(header.nTiles));
    int bigRes = collateStride * header.tileRes;
    ImageSpec bigSpec(bigRes, bigRes, 4, TypeDesc::UINT8 );
    ImageBuf *bigBuf = new ImageBuf( "big", bigSpec);


    // Loop through tiles copying the data to our image buffer
    for(int i = 0; i < header.nTiles; ++i ) {
        // Pull data
        tileBuf = getTile(i);

        int dx = header.tileRes * (i % collateStride);
        int dy = header.tileRes * (i / collateStride);

        ImageBufAlgo::paste(*bigBuf, dx, dy, 0, 0, *tileBuf);

        delete [] (unsigned char *)tileBuf->localpixels();
        delete tileBuf;
        if( verbose )printf("\033[0GINFO: Processing tile %i of %i.",
            i+1, header.nTiles );
    }
    if( verbose ) cout << endl;

    return bigBuf;
}

ImageBuf *
SMT::reconstructBig()
{
    if( verbose )cout << "INFO: Reconstructing Big\n";
    if( verbose )cout << "INFO: Loading tilemap from: " << tilemapFile << endl;

    //Load tilemap from SMF
    ImageBuf *tilemapBuf = NULL;
    if( is_smf( tilemapFile ) ) {
        SMF sourcesmf( tilemapFile );
        tilemapBuf = sourcesmf.getTilemap();
    }

    // Else load tilemap from image
    if(! tilemapBuf )
    {
        tilemapBuf = new ImageBuf( tilemapFile );
        tilemapBuf->read( 0, 0, true, TypeDesc::UINT );
        if(! tilemapBuf->initialized() )
        {
            delete tilemapBuf;
            if(! quiet ) printf( "ERROR: %s cannot be loaded.\n",
                tilemapFile.c_str() );
            return NULL;
        }
    }
    //TODO Else load tilemap from csv

    unsigned int *tilemap = (unsigned int *)tilemapBuf->localpixels();
    int xtiles = tilemapBuf->spec().width;
    int ztiles = tilemapBuf->spec().height;

    // allocate enough data for our large image
    ImageSpec bigSpec( xtiles * tileRes, ztiles * tileRes, 4, TypeDesc::UINT8 );
    ImageBuf *bigBuf = new ImageBuf( "big", bigSpec );

    // Loop through tile index
    ImageBuf *tileBuf = NULL;
    for( int z = 0; z < ztiles; ++z ) {
        for( int x = 0; x < xtiles; ++x ) {
            int tilenum = tilemap[z * xtiles + x];
            tileBuf = getTile(tilenum);

            int xbegin = tileRes * x;
            int ybegin = tileRes * z;
            ImageBufAlgo::paste(*bigBuf, xbegin, ybegin, 0, 0, *tileBuf);

            delete [] (unsigned char *)tileBuf->localpixels();
            delete tileBuf;
            if( verbose )printf("\033[0GINFO: Processing tile %i of %i.",
                z * xtiles + x, xtiles * ztiles );
        }
    }
    cout << endl;

    delete tilemapBuf;
    if( is_smf( tilemapFile ) ) delete [] tilemap;
    
    return bigBuf;    
}

struct coord {
    int x,y;
};

struct type {
    string imageName;
    vector<coord> coordinates;
};

void
SMT::pasteDecals(ImageBuf *bigBuf)
{
    printf("WARNING: decal pasting is not yet implemented\n");
    return;
    // load definitions file
    vector<type> list;

    // Parse the file
    printf( "INFO: Reading %s\n", decalFile.c_str() );
    ifstream decalList( decalFile.c_str() );

    string line;
    string cell;
    vector<string> tokens;
    while ( getline( decalList, line ) ) {
        stringstream lineStream( line );
        tokens.clear();
        while( getline( lineStream, cell, ',' ) ) tokens.push_back( cell );

        if(tokens.size() <2 ) continue;

        // use parsed line
        coord xy;
        type *decal;
        // search for existing decal filename
        bool found = false;
        for(unsigned int i = 0; i < list.size(); ++i ) {
            if( !list[i].imageName.compare( tokens[0].c_str() ) ) {
                decal = &list[i];
                found = true;
                break;
            }
        }
        if( !found ) {
            decal = new type;
            decal->imageName = tokens[0].c_str();
        }
        xy.x = atoi( tokens[1].c_str() );
        xy.y = atoi( tokens[2].c_str() );
        decal->coordinates.push_back(xy);
        if( !found ) list.push_back(*decal);
    }

    // loop through list of decals.
    for(unsigned int i = 0; i < list.size(); ++i ) {
        if( verbose )printf("     %i %s ", (int)list[i].coordinates.size(),
            list[i].imageName.c_str() );

        ImageBuf decalBuf( list[i].imageName );
        decalBuf.read( 0, 0, false, TypeDesc::FLOAT );
        if( !decalBuf.initialized() ) {
            printf("ERROR: could not load %s\n", list[i].imageName.c_str() );
            continue;
        }
        ImageSpec decalSpec = decalBuf.spec();

        printf("(%i,%i)%i\n", decalSpec.width, decalSpec.height,
            decalSpec.nchannels );

        // loop through list of coordinates
        for(unsigned int j = 0; j < list[i].coordinates.size(); ++j ) {
            coord xy = list[i].coordinates[j];

            ROI roi( xy.x, xy.x + decalSpec.width,
                    xy.y, xy.y + decalSpec.height,
                    0,1,
                    0,5);
            ImageBuf pasteBuf;
            ImageBufAlgo::crop(pasteBuf, *bigBuf, roi);

            ImageBuf compBuf;
            if( !ImageBufAlgo::over( compBuf, decalBuf, pasteBuf) ) {
                cout << "ERROR with composite: ";
                cout << compBuf.geterror() << endl;
                continue;
            }
            char filename[256];
            sprintf(filename, "test%i.png", j);
            compBuf.save(filename);
        }
    }
    return;
}
