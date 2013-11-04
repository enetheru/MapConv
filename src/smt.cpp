#include "smt.h"

#include <fstream>

TileMip::TileMip()
{
	n = '\0';
}

SMTHeader::SMTHeader()
:	version(1),
	tileRes(32),
	comp(1)
{
	strcpy(magic,"spring tilefile");
}

SMT::SMT(bool v, bool q, int c) : verbose(v), quiet(q), slowcomp(c), tileRes(32)
{
	nTiles = 0;
	setType(1);
	return;
}

void SMT::setPrefix(string prefix) { outPrefix = prefix.c_str(); }
void SMT::setTileindex(string filename) { tilemapFile = filename.c_str(); }
void SMT::setRes(int r) { tileRes = r; }
void SMT::setDim(int w, int l) { width = w; length = l; }

void
SMT::setType(int comp)
{
	this->comp = comp;
	tileSize = 0;
	// Calculate the size of the tile data
	// size is the raw format of dxt1 with 4 mip levels
	// 32x32, 16x16, 8x8, 4x4
	// 512  + 128  + 32 + 8 = 680
	if(comp == DXT1) {
		int mip = tileRes;
		for( int i=0; i < 4; ++i) {
			tileSize += mip/4 * mip/4 * 8;
			mip /= 2;
		}
	} else {
		if( !quiet )printf("ERROR: '%i' is not a valid compression type", comp);
		tileSize = -1;
	}
	if( verbose )printf("INFO: TileSize: %i bytes\n", tileSize);
	
	return;
}

void SMT::addImage(string filename) { sourceFiles.push_back(filename); }

bool
SMT::load(string fileName)
{
	loadFile = fileName;
	ifstream inFile;
	char magic[16];
	bool fail = false;
	// Test File
	if( fileName.compare("") ) {
		inFile.open(fileName.c_str(), ifstream::in);
		if(!inFile.good()) {
			if ( !quiet )printf( "ERROR: Cannot open '%s'\n", fileName.c_str() ); 
			fail = true;
		} else {
			inFile.read(magic, 16);
			if( strcmp(magic, "spring tilefile") ) {
				if ( !quiet )printf( "ERROR: %s is not a valid smt\n", fileName.c_str() );
				fail = true;
			}
		}
	} //fileName.compare("")
	if ( fail ) {
		inFile.close();
		return fail;

	}

	inFile.seekg(0);
	inFile.read( (char *)&header, sizeof(SMTHeader) );

	if( verbose ) {
		cout << "INFO: " << fileName << endl;
		cout << "\tSMT Version: " << header.version << endl;
		cout << "\tTiles: " << header.nTiles << endl;
		cout << "\tTileRes: " << header.tileRes << endl;
		cout << "\tCompression: ";
		if(header.comp == DXT1)cout << "dxt1" << endl;
		else {
			cout << "UNKNOWN" << endl;
			fail = true;
		}
	}
	setType(header.comp);

	inFile.close();
	return fail;
}

bool
SMT::decompileTiles()
{
	bool fail = false;
	ImageOutput *imageOutput;
	ImageSpec *imageSpec;
	unsigned char *tile_data;
	unsigned char *pixels;

	if( !loadFile.compare("")) {
		if( !quiet )cout << "ERROR: no smt file loaded" << endl;
		return true;
	}

	ifstream inFile(loadFile.c_str(), ifstream::in);
	inFile.seekg( sizeof(SMTHeader) );

	// Save each tile to an individual file
	for(int i = 0; i < header.nTiles && !fail; ++i ) {
		// Pull data
		tile_data = new unsigned char[tileSize];
		inFile.read( (char *)tile_data, tileSize);
		if(header.comp == DXT1)
			pixels = dxt1_load(tile_data, header.tileRes, header.tileRes);
		delete [] tile_data;

		char filename[256];
		sprintf(filename, "%s_tile_%010i.jpg", outPrefix.c_str(), i);

		imageOutput = NULL;
		imageOutput = ImageOutput::create(filename);
		if(!imageOutput) fail = true;
		else {
			imageSpec = new ImageSpec(header.tileRes, header.tileRes, 4, TypeDesc::UINT8);
			imageOutput->open( filename, *imageSpec );
			imageOutput->write_image( TypeDesc::UINT8, pixels);
			imageOutput->close();
			delete imageOutput;
			delete imageSpec;
		}
		delete [] pixels;
	}
	return fail;
}

bool
SMT::decompileCollate()
{
	if( !loadFile.compare("") ) {
		if( !quiet )cout << "ERROR: No SMT Loaded." << endl;
		return true;
	}
	// OpenImageIO
	ImageOutput *imageOutput = NULL;
	ImageSpec *imageSpec = NULL;
	int xres, yres, channels;
	int tileindex_w, tileindex_l;
	unsigned char *pixels;
	unsigned char *tpixels;
	unsigned char *tileData;
	ifstream infile;

	// Allocate Image size large enough to accomodate the tiles,
	tileindex_w = tileindex_l = ceil(sqrt(header.nTiles));
	xres = yres = tileindex_w * header.tileRes;
	channels = 4;
	pixels = new unsigned char[xres * yres * channels];

	char filename[256];
	sprintf(filename, "%s_tiles.jpg", loadFile.c_str());

	imageOutput = NULL;
	imageOutput = ImageOutput::create(filename);
	if(!imageOutput) return true;
	imageSpec = new ImageSpec(xres, yres, channels, TypeDesc::UINT8);

	// Loop through tiles copying the data to our image buffer
	//TODO save scanlines to file once a row is complete to
	//save on memory
	infile.open(loadFile.c_str(), ifstream::in);
	infile.seekg( sizeof(SMTHeader) );
	for(int i = 0; i < header.nTiles; ++i ) {
		// Pull data
		tileData = new unsigned char[tileSize];
		infile.read( (char *)tileData, tileSize);
		if ( header.comp == DXT1 ) {
			tpixels = dxt1_load(tileData, header.tileRes, header.tileRes);
		}
		delete [] tileData;

		for ( int y = 0; y < header.tileRes; y++ ) {
			for ( int x = 0; x < header.tileRes; x++ ) {
				// get pixel locations.
				int tp = (y * header.tileRes + x) * 4;
				int dx = header.tileRes * (i % tileindex_w) + x;
				int dy = header.tileRes * (i / tileindex_w) + y;
				int dp = (dy * xres + dx) * 4;

				memcpy( (char *)&pixels[dp], (char *)&tpixels[tp], 4 );
			}
		}
		delete [] tpixels;
	}
	infile.close();
	imageOutput->open( filename, *imageSpec );
	imageOutput->write_image( TypeDesc::UINT8, pixels);
	imageOutput->close();
	delete imageOutput;
	delete imageSpec;
	delete [] pixels;
	return false;
}

bool
SMT::decompileReconstruct()
{
	// OpenImageIO
	ImageOutput *imageOutput = NULL;
	ImageSpec *imageSpec = NULL;
	unsigned char *pixels;
	unsigned char *tpixels;
	unsigned char *tileData;
	ifstream infile;

	//TODO, load tile map from csv
	ImageBuf *imageBuf = NULL;
	if( is_smf( tilemapFile ) ) {
		SMF sourcesmf(tilemapFile);
		imageBuf = sourcesmf.getTilemap();
	}
	if( !imageBuf ) {
		imageBuf = new ImageBuf( tilemapFile );
		imageBuf->read(0,0,false,TypeDesc::UINT);
		if( !imageBuf->initialized() ) {
			delete imageBuf;
			return true;
		}
	}

	unsigned int *tilemap = (unsigned int *)imageBuf->localpixels();
	int xres = imageBuf->spec().width;
	int yres = imageBuf->spec().height;

	// allocate enough data for our large image
	pixels = new unsigned char[xres * 32 * yres * 32 * 4];

	// Loop through tile index
	infile.open( loadFile.c_str(), ifstream::in );
	for( int ty = 0; ty < yres; ++ty ) {
		for( int tx = 0; tx < xres; ++tx ) {
			// allocate data for the tile
			tileData = new unsigned char[tileSize];

			// get the index value	
			int tilenum = tilemap[ty * xres + tx];

			// Pull data from the tile file
			infile.seekg( sizeof(SMTHeader) + tilenum * tileSize );
			infile.read( (char *)tileData, tileSize);
			if ( header.comp == DXT1 )
				tpixels = dxt1_load(tileData, header.tileRes, header.tileRes);
			delete [] tileData;

			// Copy to our large image
			// TODO save scanlines to large image once a row is filled to save memory
			for ( int y = 0; y < header.tileRes; y++ ) {
				for ( int x = 0; x < header.tileRes; x++ ) {
					// get pixel locations.
					int tp = (y * header.tileRes + x) * 4;
					int dx = header.tileRes * tx + x;
					int dy = header.tileRes * ty + y;
					int dp = (dy * xres * 32 + dx) * 4;
						memcpy( (char *)&pixels[dp], (char *)&tpixels[tp], 4 );
				}
			}
			delete [] tpixels;
		}
	}
	infile.close();

	char filename[256];
	sprintf(filename, "%s_tiles.jpg", outPrefix.c_str());

	// Save the large image to disk
	imageOutput = NULL;
	imageOutput = ImageOutput::create(filename);
	if(!imageOutput) return true;

	imageSpec = new ImageSpec(xres * 32, yres * 32, 4, TypeDesc::UINT8);
	imageOutput->open( filename, *imageSpec );
	imageOutput->write_image( TypeDesc::UINT8, pixels);
	imageOutput->close();
	delete imageOutput;
	delete imageSpec;
	delete [] pixels;

	return false;
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
	char filename[256];
	sprintf(filename, "%s.smt", outPrefix.c_str());

	if( verbose ) printf("\nINFO: Creating %s\n", filename );
	fstream smt( filename, ios::binary | ios::out );
	if( !smt.good() ) {
		cout << "ERROR: fstream error." << endl;
		return true;
	}

	SMTHeader header;
	header.tileRes = tileRes;
	header.comp = comp;
	if( verbose ) {
		printf( "\tVersion: %i.\n", header.version);
		printf( "\tnTiles: tbc.\n");
		printf( "\ttileRes: (%i,%i)%i.\n", tileRes, tileRes, 4);
		printf( "\tcompType: %i.\n", comp);
		printf( "\ttileSize: %i bytes.\n", tileSize);
	}

	smt.write( (char *)&header, sizeof(SMTHeader) );
	smt.close();

	// Build Tileindex & Tiles //
	/////////////////////////////
	
	// setup size for index dimensions
	int tcx = width * 16; // tile count x
	int tcz = length * 16; // tile count z
	unsigned int *indexPixels = new unsigned int[tcx * tcz];

	// Load source image
	ImageBuf imageBuf( sourceFiles[0].c_str() );
	imageBuf.read(0, 0, false, TypeDesc::UINT8);
	const ImageSpec *imageSpec;
	imageSpec = &imageBuf.spec();

	// source tile data
	unsigned char *std = NULL; 
	int stw_f = (float)imageSpec->width / tcx;
	int stl_f = (float)imageSpec->height / tcz;
	int stw = (int)stw_f;
	int stl = (int)stl_f;
	int stc = imageSpec->nchannels;

	NVTTOutputHandler outputHandler(tileSize);
	nvtt::InputOptions inputOptions;
	nvtt::OutputOptions outputOptions;
	nvtt::CompressionOptions compressionOptions;
	nvtt::Compressor compressor;

	outputOptions.setOutputHeader(false);
	outputOptions.setOutputHandler( &outputHandler );
	compressionOptions.setFormat(nvtt::Format_DXT1a);

	if( slowcomp ) compressionOptions.setQuality(nvtt::Quality_Normal); 
	else           compressionOptions.setQuality(nvtt::Quality_Fastest); 

	vector<TileMip> tileMips;
	TileMip tileMip;

	// warn for re-sizing of source tiles.
	if( verbose ) {
		printf( "\tLoaded %s (%i, %i)%i\n",
			sourceFiles[0].c_str(), imageSpec->width, imageSpec->height, imageSpec->nchannels);
		if( stw != tileRes || stl != tileRes )
			printf( "WARNING: Converting tiles from (%i,%i)%i to (%i,%i)%i\n",
				stw,stl,stc,tileRes,tileRes,4);
	}

	// Generate and write tiles
	smt.open(filename, ios::binary | ios::out | ios::app );
	ROI roi;
	timeval t1, t2;
    double elapsedTime;
	deque<double> readings;
	double averageTime = 0;
	double intervalTime = 0;
	int totalTiles = tcx * tcz;
	int currentTile;
	// loop through tile columns
	for ( int z = 0; z < tcz; z++) {
		// loop through tile rows
		for ( int x = 0; x < tcx; x++) {
			currentTile = z * tcx + x + 1;
    		gettimeofday(&t1, NULL);

			// copy a tile from the scanline data
			roi.xbegin = (int)x * stw_f;
			roi.xend = (int)x * stw_f + stw_f;
			roi.ybegin = (int)z * stl_f;
			roi.yend = (int)z * stl_f + stl_f;
			roi.zbegin = 0;
			roi.zend = 1;
			roi.chbegin = 0;
			roi.chend = 5;

			ImageBuf tileBuf;
			ImageBufAlgo::crop( tileBuf, imageBuf, roi );
			imageSpec = &tileBuf.spec();
					
			// Fix channels
			ImageBuf fixBuf;
			bool boolean = false;
			const int mapBGR[] = {2,1,0,-1};
			const int mapBGRA[] = {2,1,0,3};
			const float fill[] = {255,255,255,255};
			if(imageSpec->nchannels < 4)
				boolean = ImageBufAlgo::channels(fixBuf, tileBuf, 4, mapBGR, fill);
			else
				boolean = ImageBufAlgo::channels(fixBuf, tileBuf, 4, mapBGRA, fill);
			if(!boolean)printf("ERROR: ImageBufAlgo::channels(...) Failed\n");
			tileBuf.clear();
			tileBuf.copy(fixBuf);
			fixBuf.clear();
			imageSpec = &tileBuf.spec();

			// Fix resolution
			if(imageSpec->width != tileRes || imageSpec->height != tileRes)
			{
				roi.xbegin = roi.ybegin = 0;
				roi.xend = roi.yend = tileRes;
				ImageBufAlgo::resample(fixBuf, tileBuf, true, roi);
				tileBuf.clear();
				tileBuf.copy(fixBuf);
				fixBuf.clear();
			}

			// get handle to raw pixel data for dxt processing
			std = (unsigned char *)tileBuf.localpixels();

			// process into dds
			outputHandler.reset();
			inputOptions.setTextureLayout( nvtt::TextureType_2D, tileRes, tileRes );
			inputOptions.setMipmapData( std, tileRes, tileRes );

			compressor.process(inputOptions, compressionOptions,
						outputOptions);
			tileBuf.clear();

			//copy mipmap to use as checksum: 64bits
			memcpy(&tileMip, &outputHandler.buffer[672], 8);

			bool match = false;
			unsigned int i; //tile index
			for( i = 0; i < tileMips.size(); i++ ) {
				if( !strcmp( (char *)&tileMips[i], (char *)&tileMip ) ) {
					match = true;
					break;
				} 
			}
			if( !match ) {
				tileMips.push_back(tileMip);
				// write tile to file.
				smt.write( outputHandler.buffer, tileSize );
				nTiles +=1;
			}
			// Write index to tilemap
			indexPixels[z * tcx + x] = i;

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
			   	printf("\033[0GINFO: Tile %i of %i %%%0.1f, %0.1fs",
				currentTile, totalTiles, (float)currentTile / totalTiles * 100, 
			    averageTime * (totalTiles - currentTile) / 1000);
				cout << "             ";
			}
		}
	}
	printf("\n");
	smt.close();

	// retroactively fix up the tile count.
	smt.open(filename, ios::binary | ios::in | ios::out );
	smt.seekp( 20);
	smt.write( (char *)&nTiles, 4);
	smt.close();

	// Save tileindex
	ImageOutput *imageOutput;
	sprintf( filename, "%s_tilemap.exr", outPrefix.c_str() );
	
	imageOutput = ImageOutput::create(filename);
	if( !imageOutput ) {
		delete [] indexPixels;
		return true;
	}

	imageSpec = new ImageSpec( tcx, tcz, 1, TypeDesc::UINT);
	imageOutput->open( filename, *imageSpec );
	imageOutput->write_image( TypeDesc::UINT, indexPixels );
	imageOutput->close();

	delete imageOutput;
	delete imageSpec;
	delete [] indexPixels;

	return false;

}
