/******************************************************************************
 * $Id$
 *
 * Project:  JPEG-2000
 * Purpose:  Partial implementation of the ISO/IEC 15444-1 standard
 * Author:   Andrey Kiselev, dron@remotesensing.org
 *
 ******************************************************************************
 * Copyright (c) 2002, Andrey Kiselev <dron@remotesensing.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ******************************************************************************
 * 
 * $Log$
 * Revision 1.10  2003/01/30 16:25:35  dron
 * Support for reading and writing GeoJP2 information via hacked JasPer.
 *
 * Revision 1.9  2003/01/30 11:59:15  dron
 * Fixes in component color space handling; several memory leaks removed.
 *
 * Revision 1.8  2003/01/28 14:44:11  dron
 * Temporary hack for reading JP2 boxes (due to lack of appropriate JasPer API).
 *
 * Revision 1.7  2002/11/23 18:54:17  warmerda
 * added CREATIONDATATYPES metadata for drivers
 *
 * Revision 1.6  2002/10/10 11:31:12  dron
 * Fix for buiding GDAL with JasPer software under Windows.
 *
 * Revision 1.5  2002/10/08 07:58:05  dron
 * Added encoding options.
 *
 * Revision 1.4  2002/10/04 15:13:25  dron
 * WORLDFILE support
 *
 * Revision 1.3  2002/09/23 15:47:00  dron
 * CreateCopy() enabled.
 *
 * Revision 1.2  2002/09/19 14:50:02  warmerda
 * added debug statement
 *
 * Revision 1.1  2002/09/18 16:49:01  dron
 * Initial release
 *
 *
 *
 */

#include "gdal_priv.h"
#include "cpl_string.h"

#define uchar unsigned char
#define longlong long long
#define ulonglong unsigned long long
#include <jasper/jasper.h>

CPL_CVSID("$Id$");

CPL_C_START
void	GDALRegister_JPEG2000(void);
#ifdef HAVE_JASPER_UUID
CPLErr CPL_DLL GTIFMemBufFromWkt( const char *pszWKT, 
                                  const double *padfGeoTransform,
                                  int nGCPCount, const GDAL_GCP *pasGCPList,
                                  int *pnSize, unsigned char **ppabyBuffer );
CPLErr CPL_DLL GTIFWktFromMemBuf( int nSize, unsigned char *pabyBuffer, 
                          char **ppszWKT, double *padfGeoTransform,
                          int *pnGCPCount, GDAL_GCP **ppasGCPList );
#endif
CPL_C_END

// XXX: Part of code below extracted from the JasPer internal headers and
// must be in sync with JasPer version (this one works with JasPer 1.600.0)
#define	JP2_FTYP_MAXCOMPATCODES	32
#define	JP2_BOX_IHDR	0x69686472	/* Image Header */
#define	JP2_BOX_BPCC	0x62706363	/* Bits Per Component */
#define	JP2_BOX_UUID	0x75756964	/* UUID */
extern "C" {
typedef struct {
	uint_fast32_t magic;
} jp2_jp_t;
typedef struct {
	uint_fast32_t majver;
	uint_fast32_t minver;
	uint_fast32_t numcompatcodes;
	uint_fast32_t compatcodes[JP2_FTYP_MAXCOMPATCODES];
} jp2_ftyp_t;
typedef struct {
	uint_fast32_t width;
	uint_fast32_t height;
	uint_fast16_t numcmpts;
	uint_fast8_t bpc;
	uint_fast8_t comptype;
	uint_fast8_t csunk;
	uint_fast8_t ipr;
} jp2_ihdr_t;
typedef struct {
	uint_fast16_t numcmpts;
	uint_fast8_t *bpcs;
} jp2_bpcc_t;
typedef struct {
	uint_fast8_t method;
	uint_fast8_t pri;
	uint_fast8_t approx;
	uint_fast32_t csid;
	uint_fast8_t *iccp;
	int iccplen;
} jp2_colr_t;
typedef struct {
	uint_fast16_t numlutents;
	uint_fast8_t numchans;
	int_fast32_t *lutdata;
	uint_fast8_t *bpc;
} jp2_pclr_t;
typedef struct {
	uint_fast16_t channo;
	uint_fast16_t type;
	uint_fast16_t assoc;
} jp2_cdefchan_t;
typedef struct {
	uint_fast16_t numchans;
	jp2_cdefchan_t *ents;
} jp2_cdef_t;
typedef struct {
	uint_fast16_t cmptno;
	uint_fast8_t map;
	uint_fast8_t pcol;
} jp2_cmapent_t;

typedef struct {
	uint_fast16_t numchans;
	jp2_cmapent_t *ents;
} jp2_cmap_t;

#ifdef HAVE_JASPER_UUID
typedef struct {
	uint_fast32_t data_len;
	uint_fast8_t uuid[16];
	uint_fast8_t *data;
} jp2_uuid_t;
#endif

struct jp2_boxops_s;
typedef struct {

	struct jp2_boxops_s *ops;
	struct jp2_boxinfo_s *info;

	uint_fast32_t type;
	uint_fast32_t len;
#ifdef HAVE_JASPER_UUID
	uint_fast32_t data_len;
#endif

	union {
		jp2_jp_t jp;
		jp2_ftyp_t ftyp;
		jp2_ihdr_t ihdr;
		jp2_bpcc_t bpcc;
		jp2_colr_t colr;
		jp2_pclr_t pclr;
		jp2_cdef_t cdef;
		jp2_cmap_t cmap;
#ifdef HAVE_JASPER_UUID
		jp2_uuid_t uuid;
#endif
	} data;

} jp2_box_t;
typedef struct jp2_boxops_s {
	void (*init)(jp2_box_t *box);
	void (*destroy)(jp2_box_t *box);
	int (*getdata)(jp2_box_t *box, jas_stream_t *in);
	int (*putdata)(jp2_box_t *box, jas_stream_t *out);
	void (*dumpdata)(jp2_box_t *box, FILE *out);
} jp2_boxops_t;

extern jp2_box_t *jp2_box_create(int type);
extern void jp2_box_destroy(jp2_box_t *box);
extern jp2_box_t *jp2_box_get(jas_stream_t *in);
extern int jp2_box_put(jp2_box_t *box, jas_stream_t *out);
#ifdef HAVE_JASPER_UUID
int jp2_encode_uuid(jas_image_t *image, jas_stream_t *out,
		    char *optstr, jp2_box_t *uuid);
#endif
}
// XXX: End of JasPer header.

#ifdef HAVE_JASPER_UUID
// Magick sequence for GeoJP2 box
static unsigned char msi_uuid2[16] =
	{0xb1,0x4b,0xf8,0xbd,0x08,0x3d,0x4b,0x43,
         0xa5,0xae,0x8c,0xd7,0xd5,0xa6,0xce,0x03}; 
#endif

/************************************************************************/
/* ==================================================================== */
/*				JPEG2000Dataset				*/
/* ==================================================================== */
/************************************************************************/

class JPEG2000Dataset : public GDALDataset
{
    friend class JPEG2000RasterBand;

    FILE	*fp;
    jas_stream_t *sStream;
    jas_image_t	*sImage;
    int		iFormat;

    char	*pszProjection;
    int		bGeoTransformValid;
    double	adfGeoTransform[6];
    int		nGCPCount;
    GDAL_GCP    *pasGCPList;

  public:
                JPEG2000Dataset();
		~JPEG2000Dataset();
    
    static GDALDataset	*Open( GDALOpenInfo * );

    CPLErr		GetGeoTransform( double* );
    virtual const char	*GetProjectionRef(void);
    virtual int		GetGCPCount();
    virtual const char	*GetGCPProjection();
    virtual const GDAL_GCP *GetGCPs();
};

/************************************************************************/
/* ==================================================================== */
/*                            JPEG2000RasterBand                        */
/* ==================================================================== */
/************************************************************************/

class JPEG2000RasterBand : public GDALRasterBand
{
    friend class JPEG2000Dataset;

    jas_matrix_t	*sMatrix;

  public:

    		JPEG2000RasterBand( JPEG2000Dataset *, int, int, int );
    		~JPEG2000RasterBand();
		
    virtual CPLErr IReadBlock( int, int, void * );
};


/************************************************************************/
/*                           JPEG2000RasterBand()                       */
/************************************************************************/

JPEG2000RasterBand::JPEG2000RasterBand( JPEG2000Dataset *poDS, int nBand,
		int iDepth, int bSignedness )

{
    this->poDS = poDS;
    this->nBand = nBand;

    // XXX: JasPer can't handle data with depth > 32 bits
    // Maximum possible depth for JPEG2000 is 38!
    switch ( bSignedness )
    {
	case 1:				// Signed component
	if (iDepth <= 8)
	    this->eDataType = GDT_Byte; // FIXME: should be signed,
					// but we haven't signed byte
					// data type in GDAL
	else if (iDepth <= 16)
            this->eDataType = GDT_Int16;
	else if (iDepth <= 32)
            this->eDataType = GDT_Int32;
	break;
	case 0:				// Unsigned component
	default:
	if (iDepth <= 8)
	    this->eDataType = GDT_Byte;
	else if (iDepth <= 16)
            this->eDataType = GDT_UInt16;
	else if (iDepth <= 32)
            this->eDataType = GDT_UInt32;
	break;
    }

    nBlockXSize = poDS->GetRasterXSize();
    nBlockYSize = poDS->GetRasterYSize();
    sMatrix = jas_matrix_create(nBlockYSize, nBlockXSize);
    
}

/************************************************************************/
/*                             IReadBlock()                             */
/************************************************************************/

CPLErr JPEG2000RasterBand::IReadBlock( int nBlockXOff, int nBlockYOff,
                                      void * pImage )
{
    int i, j;
    JPEG2000Dataset *poGDS = (JPEG2000Dataset *) poDS;

    // Decode image from the stream, if not yet
    if ( !poGDS->sImage )
    {
	if ( !( poGDS->sImage =
	        jas_image_decode(poGDS->sStream, poGDS->iFormat, 0) ) )
	{
	    CPLDebug( "JPEG2000", "Unable to decode image.\n" );
	    return CE_Failure;
	}
    }
    
    jas_image_readcmpt( poGDS->sImage, nBand - 1, nBlockXOff, nBlockYOff,
	nBlockXSize, nBlockYSize, sMatrix );

    for( i = 0; i < jas_matrix_numrows(sMatrix); i++ )
	for( j = 0; j < jas_matrix_numcols(sMatrix); j++ )
	    // XXX: We need casting because matrix element always
	    // has 32 bit depth in JasPer
	    // FIXME: what about float values?
	    switch( eDataType )
	    {
	        case GDT_Int16:
		{
		    GInt16* ptr = (GInt16*)pImage;
		    *ptr = (GInt16)jas_matrix_get(sMatrix, i, j);
		    ptr++;
		    pImage = ptr;
		}
		break;
	        case GDT_Int32:
		{
		    GInt32* ptr = (GInt32*)pImage;
		    *ptr = (GInt32)jas_matrix_get(sMatrix, i, j);
		    ptr++;
		    pImage = ptr;
		}
		break;
	        case GDT_UInt16:
		{
		    GUInt16* ptr = (GUInt16*)pImage;
		    *ptr = (GUInt16)jas_matrix_get(sMatrix, i, j);
		    ptr++;
		    pImage = ptr;
		}
		break;
	        case GDT_UInt32:
		{
		    GUInt32* ptr = (GUInt32*)pImage;
		    *ptr = (GUInt32)jas_matrix_get(sMatrix, i, j);
		    ptr++;
		    pImage = ptr;
		}
		break;
	        case GDT_Byte:
	        default:
		{
		    GByte* ptr = (GByte*)pImage;
		    *ptr = (GByte)jas_matrix_get(sMatrix, i, j);
		    ptr++;
		    pImage = ptr;
		}
		break;
	    }

    return CE_None;
}

/************************************************************************/
/*                         ~JPEG2000RasterBand()                        */
/************************************************************************/

JPEG2000RasterBand::~JPEG2000RasterBand()

{
    jas_matrix_destroy( sMatrix );
}


/************************************************************************/
/* ==================================================================== */
/*				JPEG2000Dataset				*/
/* ==================================================================== */
/************************************************************************/

/************************************************************************/
/*                           JPEG2000Dataset()                           */
/************************************************************************/

JPEG2000Dataset::JPEG2000Dataset()

{
    fp = NULL;
    sStream = 0;
    sImage = 0;
    nBands = 0;
    pszProjection = CPLStrdup("");
    nGCPCount = 0;
    pasGCPList = NULL;
    bGeoTransformValid = FALSE;
    adfGeoTransform[0] = 0.0;
    adfGeoTransform[1] = 1.0;
    adfGeoTransform[2] = 0.0;
    adfGeoTransform[3] = 0.0;
    adfGeoTransform[4] = 0.0;
    adfGeoTransform[5] = 1.0;
}

/************************************************************************/
/*                            ~JPEG2000Dataset()                         */
/************************************************************************/

JPEG2000Dataset::~JPEG2000Dataset()

{
    if ( sStream )
	jas_stream_close( sStream );
    if ( sImage )
	jas_image_destroy( sImage );
    jas_image_clearfmts();
    if ( pszProjection )
	CPLFree( pszProjection );
    if( nGCPCount > 0 )
    {
        for( int i = 0; i < nGCPCount; i++ )
	    if ( pasGCPList[i].pszId )
		CPLFree( pasGCPList[i].pszId );

        CPLFree( pasGCPList );
    }
    if( fp != NULL )
        VSIFClose( fp );
}

/************************************************************************/
/*                          GetProjectionRef()                          */
/************************************************************************/

const char *JPEG2000Dataset::GetProjectionRef()

{
    return( pszProjection );
}

/************************************************************************/
/*                          GetGeoTransform()                           */
/************************************************************************/

CPLErr JPEG2000Dataset::GetGeoTransform( double * padfTransform )
{
    if( bGeoTransformValid )
    {
        memcpy( padfTransform, adfGeoTransform, sizeof(adfGeoTransform[0]) * 6 );
        return CE_None;
    }
    else
        return CE_Failure;
}

/************************************************************************/
/*                            GetGCPCount()                             */
/************************************************************************/

int JPEG2000Dataset::GetGCPCount()

{
    return nGCPCount;
}

/************************************************************************/
/*                          GetGCPProjection()                          */
/************************************************************************/

const char *JPEG2000Dataset::GetGCPProjection()

{
    if( nGCPCount > 0 )
        return pszProjection;
    else
        return "";
}

/************************************************************************/
/*                               GetGCP()                               */
/************************************************************************/

const GDAL_GCP *JPEG2000Dataset::GetGCPs()

{
    return pasGCPList;
}

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

GDALDataset *JPEG2000Dataset::Open( GDALOpenInfo * poOpenInfo )

{
    int		iFormat;
    char	*pszFormatName = NULL;
    jas_stream_t *sS;

    if( poOpenInfo->fp == NULL )
        return NULL;
   
    jas_init();
    if( !(sS = jas_stream_fopen( poOpenInfo->pszFilename, "rb" )) )
    {
	jas_image_clearfmts();
	return NULL;
    }
    iFormat = jas_image_getfmt( sS );
    if ( !(pszFormatName = jas_image_fmttostr( iFormat )) )
    {
	jas_stream_close( sS );
	jas_image_clearfmts();
	return NULL;
    }
    if ( strlen( pszFormatName ) < 3 ||
        (!EQUALN( pszFormatName, "jp2", 3 ) &&
	 !EQUALN( pszFormatName, "jpc", 3 ) &&
	 !EQUALN( pszFormatName, "pgx", 3 )) )
    {
        CPLDebug( "JPEG2000", "JasPer reports file is format type `%s'.", 
                  pszFormatName );
        jas_stream_close( sS );
	jas_image_clearfmts();
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Create a corresponding GDALDataset.                             */
/* -------------------------------------------------------------------- */
    JPEG2000Dataset 	*poDS;
    int			*paiDepth = NULL, *pabSignedness = NULL;
    int			iBand;

    poDS = new JPEG2000Dataset();

    poDS->fp = poOpenInfo->fp;
    poOpenInfo->fp = NULL;
    poDS->sStream = sS;
    poDS->iFormat = iFormat;

    if ( EQUALN( pszFormatName, "jp2", 3 ) )
    {
// XXX: Hack to read JP2 boxes from input file. JasPer hasn't public API
// call for such things, so we use internal JasPer functions.
	jp2_box_t *box;
	box = 0;
	while ( ( box = jp2_box_get(poDS->sStream) ) )
	{
	    switch (box->type)
	    {
		case JP2_BOX_IHDR:
		poDS->nBands = box->data.ihdr.numcmpts;
		poDS->nRasterXSize = box->data.ihdr.width;
		poDS->nRasterYSize = box->data.ihdr.height;
		CPLDebug( "JPEG2000",
			  "IHDR box found. Dump: "
			  "width=%d, height=%d, numcmpts=%d, bpp=%d\n",
			  box->data.ihdr.width, box->data.ihdr.height,
			  box->data.ihdr.numcmpts, box->data.ihdr.bpc );
		if ( box->data.ihdr.bpc )
		{
		    paiDepth = (int *)
			CPLMalloc(box->data.bpcc.numcmpts * sizeof(int));
		    pabSignedness = (int *)
			CPLMalloc(box->data.bpcc.numcmpts * sizeof(int));
		    for ( iBand = 0; iBand < poDS->nBands; iBand++ )
		    {
			paiDepth[iBand] = box->data.ihdr.bpc && 0x7F;
			pabSignedness[iBand] = box->data.ihdr.bpc >> 7;
			CPLDebug( "JPEG2000",
				  "Component %d: bpp=%d, signedness=%d",
				  iBand, paiDepth[iBand], pabSignedness[iBand] );
		    }
		}
		break;
		case JP2_BOX_BPCC:
		CPLDebug( "JPEG2000", "BPCC box found. Dump:" );
		if ( !paiDepth )
 		{
			CPLMalloc( box->data.bpcc.numcmpts * sizeof(int) );
		    pabSignedness = (int *)
			CPLMalloc( box->data.bpcc.numcmpts * sizeof(int) );
		    for ( iBand = 0; iBand < box->data.bpcc.numcmpts; iBand++ )
		    {
			paiDepth[iBand] = box->data.bpcc.bpcs[iBand] && 0x7F;
			pabSignedness[iBand] = box->data.bpcc.bpcs[iBand] >> 7;
			CPLDebug( "JPEG2000",
				  "Component %d: bpp=%d, signedness=%d",
				  iBand, paiDepth[iBand], pabSignedness[iBand] );
		    }
		}
		break;
#ifdef HAVE_JASPER_UUID
		case JP2_BOX_UUID: // Box for custom data
		CPLDebug( "JPEG2000", "UUID box found. Length=%d", box->len );
		if( memcmp( box->data.uuid.uuid, msi_uuid2, 16 ) == 0 )
                {
		    CPLDebug( "JPEG2000", "GeoTIFF info found." );
		    if( box->data.uuid.data != NULL )
		    {
			if ( poDS->pszProjection )
			{
			    CPLFree( poDS->pszProjection );
			    poDS->pszProjection = NULL;
			}
			GTIFWktFromMemBuf( box->data.uuid.data_len,
			    box->data.uuid.data,
			    &(poDS->pszProjection), poDS->adfGeoTransform,
			    &(poDS->nGCPCount), &(poDS->pasGCPList) );
			CPLDebug( "JPEG2000", "Got projection: %s",
			      poDS->pszProjection );
			poDS->bGeoTransformValid = TRUE;
		    }
                }
		break;
#endif
	    }
	    jp2_box_destroy( box );
	    box = 0;
	}
	jas_stream_rewind(poDS->sStream);
    }
    else
    {
	if ( !(poDS->sImage = jas_image_decode(poDS->sStream, poDS->iFormat, 0)) )
	{
	    jas_stream_close( sS );
	    delete poDS;
	    CPLDebug( "JPEG2000", "Unable to decode image %s.\n", 
		      poOpenInfo->pszFilename );
	    return NULL;
	}

	poDS->nBands = jas_image_numcmpts( poDS->sImage );
	poDS->nRasterXSize = jas_image_cmptwidth( poDS->sImage, 0 );
	poDS->nRasterYSize = jas_image_cmptheight( poDS->sImage, 0 );
	paiDepth = (int *)CPLMalloc( poDS->nBands * sizeof(int) );
	pabSignedness = (int *)CPLMalloc( poDS->nBands * sizeof(int) );
	for ( iBand = 0; iBand < poDS->nBands; iBand++ )
	{
	    paiDepth[iBand] = jas_image_cmptprec( poDS->sImage, iBand );
	    pabSignedness[iBand] = jas_image_cmptsgnd( poDS->sImage, iBand );
	}
    }

/* -------------------------------------------------------------------- */
/*      Create band information objects.                                */
/* -------------------------------------------------------------------- */
    for( iBand = 1; iBand <= poDS->nBands; iBand++ )
    {
        poDS->SetBand( iBand, new JPEG2000RasterBand( poDS, iBand,
	    paiDepth[iBand - 1], pabSignedness[iBand - 1] ) );
        
    }
    
    if ( paiDepth )
	CPLFree( paiDepth );
    if ( pabSignedness )
	CPLFree( pabSignedness );

/* -------------------------------------------------------------------- */
/*      Check for world file.                                           */
/* -------------------------------------------------------------------- */
    if( !poDS->bGeoTransformValid )
    {
	poDS->bGeoTransformValid = 
	    GDALReadWorldFile( poOpenInfo->pszFilename, ".wld", 
			       poDS->adfGeoTransform );
    }

    return( poDS );
}

/************************************************************************/
/*                      JPEG2000CreateCopy()                            */
/************************************************************************/

static GDALDataset *
JPEG2000CreateCopy( const char * pszFilename, GDALDataset *poSrcDS, 
                    int bStrict, char ** papszOptions, 
                    GDALProgressFunc pfnProgress, void * pProgressData )

{
    int  nBands = poSrcDS->GetRasterCount();
    int  nXSize = poSrcDS->GetRasterXSize();
    int  nYSize = poSrcDS->GetRasterYSize();

    if( !pfnProgress( 0.0, NULL, pProgressData ) )
        return NULL;

/* -------------------------------------------------------------------- */
/*      Create the dataset.                                             */
/* -------------------------------------------------------------------- */
    int			iBand;
    jas_stream_t	*sStream;
    jas_image_t		*sImage;

    jas_init();
    if( !(sStream = jas_stream_fopen( pszFilename, "w+b" )) )
    {
        CPLError( CE_Failure, CPLE_FileIO, "Unable to create file %s.\n", 
                  pszFilename );
        return NULL;
    }
    
    if ( !(sImage = jas_image_create0()) )
    {
        CPLError( CE_Failure, CPLE_OutOfMemory, "Unable to create image %s.\n", 
                  pszFilename );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Loop over image, copying image data.                            */
/* -------------------------------------------------------------------- */
    GDALRasterBand	*poBand;
    GUInt32		*paiScanline;
    int         	iLine, iPixel;
    CPLErr      	eErr = CE_None;
    jas_matrix_t	*sMatrix;
    jas_image_cmptparm_t *sComps; // Array of pointers to image components

    sComps = (jas_image_cmptparm_t*)
	CPLMalloc( nBands * sizeof(jas_image_cmptparm_t) );
  
    if ( !(sMatrix = jas_matrix_create( 1, nXSize )) )
    {
        CPLError( CE_Failure, CPLE_OutOfMemory, 
                  "Unable to create matrix with size %dx%d.\n", 1, nYSize );
	CPLFree( sComps );
	jas_image_destroy( sImage );
        return NULL;
    }
    paiScanline = (GUInt32 *) CPLMalloc( nXSize *
                            GDALGetDataTypeSize(GDT_UInt32) / 8 );
    
    for ( iBand = 0; iBand < nBands; iBand++ )
    {
        poBand = poSrcDS->GetRasterBand( iBand + 1);
        
	sComps[iBand].tlx = sComps[iBand].tly = 0;
	sComps[iBand].hstep = sComps[iBand].vstep = 1;
	sComps[iBand].width = nXSize;
	sComps[iBand].height = nYSize;
	sComps[iBand].prec = GDALGetDataTypeSize( poBand->GetRasterDataType() );
	switch ( poBand->GetRasterDataType() )
	{
	    case GDT_Int16:
	    case GDT_Int32:
	    case GDT_Float32:
	    case GDT_Float64:
            sComps[iBand].sgnd = 1;
	    break;
	    case GDT_Byte:
	    case GDT_UInt16:
	    case GDT_UInt32:
	    default:
	    sComps[iBand].sgnd = 0;
	    break;
	}
	jas_image_addcmpt(sImage, iBand, sComps);

	for( iLine = 0; eErr == CE_None && iLine < nYSize; iLine++ )
        {
            eErr = poBand->RasterIO( GF_Read, 0, iLine, nXSize, 1, 
                              paiScanline, nXSize, 1, GDT_UInt32,
                              sizeof(GUInt32), sizeof(GUInt32) * nXSize );
            for ( iPixel = 0; iPixel < nXSize; iPixel++ )
	        jas_matrix_setv( sMatrix, iPixel, paiScanline[iPixel] );
	    
            if( (jas_image_writecmpt(sImage, iBand, 0, iLine,
			      nXSize, 1, sMatrix)) < 0 )
            {
                CPLError( CE_Failure, CPLE_AppDefined, 
                    "Unable to write scanline %d of the component %d.\n", 
                    iLine, iBand );
		jas_matrix_destroy( sMatrix );
		CPLFree( paiScanline );
		CPLFree( sComps );
		jas_image_destroy( sImage );
                return NULL;
            }
	    
            if( eErr == CE_None &&
            !pfnProgress( ((iLine + 1) + iBand * nYSize) /
			  ((double) nYSize * nBands),
			 NULL, pProgressData) )
            {
                eErr = CE_Failure;
                CPLError( CE_Failure, CPLE_UserInterrupt, 
                      "User terminated CreateCopy()" );
            }
        }
    }

/* -------------------------------------------------------------------- */
/*       Read compression parameters and encode the image.              */
/* -------------------------------------------------------------------- */
    int		    i, j;
    const int	    OPTSMAX = 4096;
    const char	    *pszFormatName;
    char	    pszOptionBuf[OPTSMAX + 1];

    const char	*apszComprOptions[]=
    {
	"imgareatlx",
	"imgareatly",
	"tilegrdtlx",
	"tilegrdtly",
	"tilewidth",
	"tileheight",
	"prcwidth",
	"prcheight",
	"cblkwidth",
	"cblkheight",
	"mode",
	"rate",
	"ilyrrates",
	"prg",
	"numrlvls",
	"sop",
	"eph",
	"lazy",
	"termall",
	"segsym",
	"vcausal",
	"pterm",
	"resetprob",
	"numgbits",
	NULL
    };
    
    pszFormatName = CSLFetchNameValue( papszOptions, "FORMAT" );
    if ( !pszFormatName ||
	 !EQUALN( pszFormatName, "jp2", 3 ) ||
	 !EQUALN( pszFormatName, "jpc", 3 ) )
	pszFormatName = "jp2";
    
    pszOptionBuf[0] = '\0';
    if ( papszOptions )
    {
	CPLDebug( "JPEG2000", "User supplied parameters:" );
	for ( i = 0; papszOptions[i] != NULL; i++ )
	{
	    CPLDebug( "JPEG2000", "%s\n", papszOptions[i] );
	    for ( j = 0; apszComprOptions[j] != NULL; j++ )
		if( EQUALN( apszComprOptions[j], papszOptions[i],
			    strlen(apszComprOptions[j]) ) )
		{
		    int m, n;

		    n = strlen( pszOptionBuf );
		    m = n + strlen( papszOptions[i] ) + 1;
		    if ( m > OPTSMAX )
			break;
		    if ( n > 0 )
		    {
			strcat( pszOptionBuf, "\n" );
		    }
		    strcat( pszOptionBuf, papszOptions[i] );
		}
	}
    }
    CPLDebug( "JPEG2000", "Parameters, delivered to the JasPer library:" );
    CPLDebug( "JPEG2000", "%s", pszOptionBuf );

    // FIXME: Improve colormodel handling
    //jas_image_setcolorspace( sImage, JAS_IMAGE_CS_UNKNOWN );
    for ( i = 0; i < nBands; i++ )
	//
	sImage->cmpts_[i]->type_ = JAS_IMAGE_CT_UNKNOWN;

/* -------------------------------------------------------------------- */
/*      Set the GeoTIFF box if georeferencing is available, and this    */
/*      is a JP2 file.                                                  */
/* -------------------------------------------------------------------- */
    if ( EQUALN( pszFormatName, "jp2", 3 ) )
    {
#ifdef HAVE_JASPER_UUID
	double	adfGeoTransform[6];
	if( ((poSrcDS->GetGeoTransform(adfGeoTransform) == CE_None
		 && (adfGeoTransform[0] != 0.0 
		     || adfGeoTransform[1] != 1.0 
		     || adfGeoTransform[2] != 0.0 
		     || adfGeoTransform[3] != 0.0 
		     || adfGeoTransform[4] != 0.0 
		     || ABS(adfGeoTransform[5]) != 1.0))
		|| poSrcDS->GetGCPCount() > 0) )
	{
	    // Prepare the memory buffer containing the degenerate GeoTIFF file.
	    const char	*pszWKT;
	    int		nGTBufSize = 0;
	    unsigned char *pabyGTBuf = NULL;
	    jp2_box_t	*box = jp2_box_create( JP2_BOX_UUID );

	    if( GDALGetGCPCount( poSrcDS ) > 0 )
		pszWKT = poSrcDS->GetGCPProjection();
	    else
		pszWKT = poSrcDS->GetProjectionRef();

	    GTIFMemBufFromWkt( pszWKT, adfGeoTransform,
			       poSrcDS->GetGCPCount(), poSrcDS->GetGCPs(),
			       &nGTBufSize, &pabyGTBuf );
 
	    memcpy( box->data.uuid.uuid, msi_uuid2, sizeof(msi_uuid2) );
	    box->data.uuid.data_len = nGTBufSize;
	    box->data.uuid.data = (uint_fast8_t *)jas_malloc( nGTBufSize );
	    memcpy( box->data.uuid.data, pabyGTBuf, nGTBufSize );
	    CPLFree( pabyGTBuf );
	    if ( jp2_encode_uuid( sImage, sStream, pszOptionBuf, box) < 0 )
	    {
		CPLError( CE_Failure, CPLE_FileIO,
			  "Unable to encode image %s.", pszFilename );
		jp2_box_destroy( box );
		jas_matrix_destroy( sMatrix );
		CPLFree( paiScanline );
		CPLFree( sComps );
		jas_image_destroy( sImage );
		jas_image_clearfmts();
		return NULL;
	    }
	jp2_box_destroy( box );
	}
	else
	{
#endif
	    if ( jp2_encode( sImage, sStream, pszOptionBuf) < 0 )
	    {
		CPLError( CE_Failure, CPLE_FileIO,
			  "Unable to encode image %s.", pszFilename );
		jas_matrix_destroy( sMatrix );
		CPLFree( paiScanline );
		CPLFree( sComps );
		jas_image_destroy( sImage );
		jas_image_clearfmts();
		return NULL;
	    }
#ifdef HAVE_JASPER_UUID
	}
#endif
    }
    else    // Write JPC code stream
    {
	if ( jpc_encode(sImage, sStream, pszOptionBuf) < 0 )
	{
	    CPLError( CE_Failure, CPLE_FileIO,
		      "Unable to encode image %s.\n", pszFilename );
	    jas_matrix_destroy( sMatrix );
	    CPLFree( paiScanline );
	    CPLFree( sComps );
	    jas_image_destroy( sImage );
	    jas_image_clearfmts();
	    return NULL;
	}
    }

    jas_stream_flush( sStream );
    
    jas_matrix_destroy( sMatrix );
    CPLFree( paiScanline );
    CPLFree( sComps );
    jas_image_destroy( sImage );
    jas_image_clearfmts();
    if ( jas_stream_close( sStream ) )
    {
        CPLError( CE_Failure, CPLE_FileIO, "Unable to close file %s.\n",
		  pszFilename );
        return NULL;
    }

/* -------------------------------------------------------------------- */
/*      Do we need a world file?                                        */
/* -------------------------------------------------------------------- */
    if( CSLFetchBoolean( papszOptions, "WORLDFILE", FALSE ) )
    {
    	double      adfGeoTransform[6];
	
	poSrcDS->GetGeoTransform( adfGeoTransform );
	GDALWriteWorldFile( pszFilename, "wld", adfGeoTransform );
    }

    return (GDALDataset *) GDALOpen( pszFilename, GA_Update );
}

/************************************************************************/
/*                        GDALRegister_JPEG2000()			*/
/************************************************************************/

void GDALRegister_JPEG2000()

{
    GDALDriver	*poDriver;

    if( GDALGetDriverByName( "JPEG2000" ) == NULL )
    {
        poDriver = new GDALDriver();
        
        poDriver->SetDescription( "JPEG2000" );
        poDriver->SetMetadataItem( GDAL_DMD_LONGNAME, 
                                   "JPEG-2000 part 1 (ISO/IEC 15444-1)" );
        poDriver->SetMetadataItem( GDAL_DMD_HELPTOPIC, 
                                   "frmt_jpeg2000.html" );
        poDriver->SetMetadataItem( GDAL_DMD_CREATIONDATATYPES, 
                                   "Byte Int16 UInt16 Int32 UInt32" );

        poDriver->pfnOpen = JPEG2000Dataset::Open;
        poDriver->pfnCreateCopy = JPEG2000CreateCopy;

        GetGDALDriverManager()->RegisterDriver( poDriver );
    }
}

