
#include <fstream>


void adjustStartTime( double currentMeanOfFinalTOFDist, string vpdParams  ){

	double dt = -currentMeanOfFinalTOFDist;

	ifstream infile;
	infile.open( vpdParams.c_str() );

	ofstream outfile;
	outfile.open( "vpd_4DB.t0_Off.dat" );

	cout << "Adjust Time by " << dt << " [ns]" << endl;
	/**
	 * Read in and write out new
	 */
	
	for ( int i = 0; i < 38; i++ ){

		int channel = -1, nBins = -1;
		infile >> channel;
		infile >> nBins;

		outfile << channel << endl;
		outfile << nBins << endl;

		//copy the bin edges
		for ( int iBin = 0; iBin <= nBins; iBin ++ ){

			double bv = -1;
			infile >> bv;
			if ( bv > 40 ) {
				cout << "Warning : Found bin edge > 40 ns, setting to 40 " << endl;
				bv = 40;
			}
			outfile << bv << " ";
		}
		outfile << endl;

		// copy the corrections
		for ( int iBin = 0; iBin <= nBins; iBin ++ ){

			double bv = -1;
			double tdt = dt;
			infile >> bv;
			if ( 0.0 == bv ){
				tdt = 0.0;
			}
			outfile << (bv + tdt) << " ";
			
		}
		outfile << endl;

	} // loop on channels




	infile.close();
	outfile.close();

}