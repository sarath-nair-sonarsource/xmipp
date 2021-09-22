 /* Authors:     Estrella Fernandez-Gimenez (me.fernandez@cnb.csic.es)*/

#include <core/multidim_array.h>
#include <core/xmipp_image.h>
#include <iostream>
#include <gtest/gtest.h>
#include "core/transformations.h"
#include <core/xmipp_fftw.h>
#include <reconstruction/volume_subtraction.cpp>

class POCSTest : public ::testing::Test
{
public:
	POCSTest()
	{
		//get results to compare
		if (chdir(((String)(getXmippPath() + (String)"/resources/test/pocs")).c_str())==-1)
			REPORT_ERROR(ERR_UNCLASSIFIED,"Cannot change directory");
		pocsmask.read("pocsmask.mrc");
		pocsnonnegative.read("pocsnonnegative.mrc");
		pocsamplitude.read("pocsamplitude.mrc");
		pocsamplitude_radavg.read("pocsamplitude_radavg.mrc");
		pocsminmax.read("pocsminmax.mrc");
		pocsphase.read("pocsphase.mrc");
	}
protected:
    virtual void SetUp()
    {
        //get example volume
        if (chdir(((String)(getXmippPath() + (String)"/resources/test/pocs")).c_str())==-1)
            REPORT_ERROR(ERR_UNCLASSIFIED,"Cannot change directory");
        img.read("V1.mrc");
    }

    Image<double> img;
    Image<double> pocsmask;
    Image<double> pocsnonnegative;
    Image<double> pocsamplitude;
    Image<double> pocsamplitude_radavg;
    Image<double> pocsminmax;
    Image<double> pocsphase;
	double min;
	double max;
	FourierTransformer transformer;
	MultidimArray< std::complex<double> > IFourier;
	MultidimArray<double> IFourierMag;
	MultidimArray<double> IFourierMag2;
	MultidimArray<double> radial_meanI;
	MultidimArray<double> radQuotient;
	MultidimArray<std::complex<double> > IFourierPhase;
	FourierTransformer transformer2;
};

/* Test for Projectors Onto Convex Sets (POCS) used for volume adjustment in program volume_subtraction.*/

TEST_F(POCSTest, pocsmask)
{
	Image<double> mask;
	mask().initZeros(XSIZE(img()),YSIZE(img()),YSIZE(img()));
	mask().initConstant(1);
	POCSmask(mask(), img());
	ASSERT_EQ(img(), pocsmask());
}

TEST_F(POCSTest, pocsnonnegative)
{
	POCSnonnegative(img());
	ASSERT_EQ(img(), pocsnonnegative());
}

TEST_F(POCSTest, pocsamplitude)
{
	transformer.completeFourierTransform(img(), IFourier);
	FFT_magnitude(IFourier,IFourierMag);
	POCSFourierAmplitude(IFourierMag, IFourier, 1);
	transformer.inverseFourierTransform();
	ASSERT_EQ(img(), pocsamplitude());
}

TEST_F(POCSTest, pocsamplituderadAvg)
{
	IFourierMag = computeMagnitude(img());
	IFourierMag2 = computeMagnitude(img());
	radQuotient = computeRadQuotient(IFourierMag, IFourierMag2, img(), img());
	transformer.FourierTransform(img(), IFourier);
	POCSFourierAmplitudeRadAvg(IFourier, 1, radQuotient, XSIZE(img()),  YSIZE(img()),  ZSIZE(img()));
	transformer.inverseFourierTransform();
	ASSERT_EQ(img(), pocsamplitude_radavg());
}

TEST_F(POCSTest, pocsminmax)
{
	img().computeDoubleMinMax(min, max);
	POCSMinMax(img(), min, max);
	ASSERT_EQ(img(), pocsminmax());
}

TEST_F(POCSTest, pocsphase)
{
	transformer.FourierTransform(img(),IFourier,false);
	transformer2.FourierTransform(img(),IFourierPhase,true);
	POCSFourierPhase(IFourierPhase,IFourier);
	transformer2.inverseFourierTransform();
	ASSERT_EQ(img(), pocsphase());
}