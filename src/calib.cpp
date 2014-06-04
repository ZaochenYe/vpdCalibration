
#include "constants.h"
#include "calib.h"
#include "histoBook.h"
#include <fstream>
#include <sstream>


/*
*	Constructor
*
* 	Parameters:
*	chain:	The chain object containing all data compatible with the TOFrPicoDST format
*	nIterations:	Max number of iterations to run
*	xmlConfig:		The xml configuration defining key aspects of the calibration
*					such as number of tot bins to use, data location etc. See repo Readme
*					for a sample configuration.		
*/
calib::calib( TChain* chain, uint nIterations, xmlConfig con )  {
	cout << "[calib.calib] " << endl;
	
	config = con;

	// default number of tot bins is defined in constants in case non is given in config file
	numTOTBins = constants::numTOTBins;
	
	// set the number of tot bins from the config if given
	if ( config.getAsInt( "numTOTBins" ) >= 1 )
		numTOTBins = config.getAsInt( "numTOTBins" );
	
	// now build arrays that need numTOTBins
	for ( int j = 0; j < constants::nChannels; j++){
		correction[ j ] 	= new double[ numTOTBins + 1 ];
		totBins[ j ] 		= new double[ numTOTBins + 1 ];

		deadDetector[ j ]	= false;
	}

	// zero the corrections & offsets
	for ( int j = 0; j < constants::nChannels; j++){
		for (int k = 0; k < numTOTBins + 1; k++){
			correction[ j ] [ k ] = 0;
		}
		initialOffsets[ j ] = 0;
		spline[ j ] = NULL;
	}
	
	// set the maximum number of iterations
	maxIterations = nIterations;
	// create the histogram book
	book = new histoBook( ( config.getAsString( "baseName" ) + config.getAsString( "rootOutput" ) ) );

	_chain = chain;
	pico = new TOFrPicoDst( _chain );

	gStyle->SetOptStat( 111 );

	currentIteration = 0;

	gErrorIgnoreLevel=kError;
	// create a canvas for report building 
	can = new TCanvas( "c", "canvas", 0, 0, 800, 1024);
	can->Print( ( config.getAsString( "baseName" ) + config.getAsString( "reportOutput" ) + "[" ).c_str() );

	//savePage();
	

}

/*
*	Destructor
*	Deletes the histoBook ensuring it is saved.
*/
calib::~calib() {
	cout << "[calib.~calib] " << endl;
	
	can->Print( ( config.getAsString( "baseName" ) + config.getAsString( "reportOutput" ) + "]" ).c_str() );


	delete book;
	
	for ( int j = 0; j < constants::nChannels; j++){
		delete [] correction[j];
		delete [] totBins[j];
		if ( spline[ j ] )
			delete spline[ j ];
	}


	

}

/*
*	Offsets
*	Calculates the initial offsets for each channel with respect to channel 1 on the west side.
*	
*/
void calib::offsets() {

	startTimer();

	if ( !_chain ){
		cout << "[calib." << __FUNCTION__ << "] ERROR: Invalid chain " << endl;
		return;
	}


	Int_t nevents = (Int_t)_chain->GetEntries();
	cout << "[calib." << __FUNCTION__ << "] Loaded: " << nevents << " events " << endl;

	book->cd( "initialOffset" );
	book->make2D( "tdc", "tdc relative to channel 0", constants::nChannels-1, 0.5, constants::nChannels + 0.5, 400, -100, 100 );
	book->make2D( "tdcRaw", "All tdc Values ", constants::nChannels-1, 1, constants::nChannels, 1000, 0, 51200 );

	cout << "[calib." << __FUNCTION__ << "] Made Histograms " << endl;

	// loop over all events
	for(Int_t i=0; i<nevents; i++) {
    	_chain->GetEntry(i);
		
		// progress indicator
    	double progress =  ((double)i / (double)nevents);
    	if ( i == nevents - 1)
    		progress = 1.001;
    	if ( i == 0 || (i % (int)(nevents / 150 )) == 0 || i == nevents - 1  ){  
			progressBar( progress, 50 );
		}
		// progress indicator

		// channel 1 on the west side is the reference channel
    	double reference = pico->vpdLeWest[0];
    	double tpcZ = pico->vertexZ;
    	
		for( int j = constants::startWest; j < constants::endEast; j++) {

			
			// skip dead detectors
			if ( deadDetector[ j ] ) continue;

			int nHits = pico->numHits( j );
			
			if ( nHits < constants::minHits ) 
				continue;

			
    		if ( pico->nTofHits <= 1 ) continue;
    		if ( TMath::Abs( tpcZ ) > 100 ) continue;

			double tdc = pico->channelTDC( j );
	    	double tot = pico->channelTOT( j );

	    
	    	book->fill( "tdcRaw", j, tdc );

	    	if(tot <= constants::minTOT || tot >= constants::maxTOT) continue;
	    

	    	book->fill( "tdc", j, tdc - reference );

	    	
		}	
	} // end loop on events

	// calculate the offsets
  	TH2D* tdc = (TH2D*) book->get( "tdc" );
  	tdc->FitSlicesY();
  	TH1D* tdcMean = (TH1D*) gDirectory->FindObject( "tdc_1" );
  	book->add( "tdcMean", tdcMean );
	
	for ( int i = constants::startWest; i < constants::endEast; i++ ){
		TH1D* tmp = tdc->ProjectionY( "tmp", i, i );
		cout << "Channel [ " << i << " ] Offset = " << tmp->GetMean() << endl;

		this->initialOffsets[ i ] = tmp->GetMean();

		delete tmp;
	}

	

	// get the east / west offset
	TH1D* west = tdc->ProjectionY( "westOffset", 2, 19 );
	TH1D* east = tdc->ProjectionY( "eastOffset", 20, 38 );

	westMinusEast = ( west->GetMean() - east->GetMean() );
	
	cout << "West - East offset: " << westMinusEast << endl; 

	gStyle->SetOptStat(0);
	
	can->Divide(1, 2);
	can->cd(1);
	
	tdcMean->SetLineColor( 2 );
	tdcMean->SetMarkerStyle( 10 );
	tdcMean->SetMarkerColor( 2 );
	tdc->Draw(  );	
	tdcMean->Draw("same");	

	can->cd(2);
	TLegend *leg = new TLegend( 0.2, 0.8, .35, .95);
	leg->AddEntry( east, "East", "l");
	leg->AddEntry( west, "West", "l");
	east->SetLineColor( 2 );

	west->Draw();
	east->Draw( "same" );
	leg->Draw();
	
	savePage();

	cout << "[calib." << __FUNCTION__ << "] completed in " << elapsed() << " seconds " << endl;
}

/*
*	binTOT
*	variableBinning:
*		false	- fixed binning the tot space from minTOT to maxTOT
*		true	- calculates variable binning for tot space such that 
*				the number of events is roughly equal for each tot bin
*
*/
void calib::binTOT( bool variableBinning ) {

	cout << "[calib." << __FUNCTION__ << "] Starting " << endl;

	if ( variableBinning )
		cout << "[calib." << __FUNCTION__ << "] Variable Binning TOT Range :  " << constants::minTOT << " -> " << constants::maxTOT << endl;
	else
		cout << "[calib." << __FUNCTION__ << "] Fixed Binning TOT Range :  " << constants::minTOT << " -> " << constants::maxTOT << endl;

	cout << "[calib." << __FUNCTION__ << "] Using " << numTOTBins << " bins for TOT" << endl;

	startTimer();

	if ( variableBinning == false ){

		for(Int_t i=0; i<constants::nChannels; i++) {
			for ( int s = 0; s <= numTOTBins; s++ ){
				double edge = ((constants::maxTOT - constants::minTOT) / (double) numTOTBins) * s;
				edge += constants::minTOT;
				totBins[ i ][ s ] = edge;
			}
		} // loop channels

	} else { // variableBinning == true ( default )

		Int_t nevents = (int)_chain->GetEntries();
		vector<double> tots[ constants::nChannels];

		cout << "[calib." << __FUNCTION__ << "] Processing " <<  nevents << " events" << endl;

		for(Int_t i=0; i<nevents; i++) {
	    	_chain->GetEntry(i);

	    	// progress indicator
	    	double progress =  ((double)i / (double)nevents);
	    	if ( i == nevents - 1)
	    		progress = 1.001;
	    	if ( i == 0 || (i % (int)(nevents / 150 )) == 0 || i == nevents - 1  ){  
				progressBar( progress, 50 );
			}
			// progress indicator

	    	Int_t numEast = pico->numberOfVpdEast;
	      	Int_t numWest = pico->numberOfVpdWest;
	     
		    if( numWest > constants::minHits){
		        
		    	for(Int_t j = 0; j < constants::endWest; j++) {
		        	Double_t tot = pico->channelTOT( j );
		          
		        	if(tot > constants::minTOT && tot < constants::maxTOT ) 
		          		tots[j].push_back(tot);
		        }

		    }

	  		if( numEast > constants::minHits ){
	    
	    		for(Int_t j = constants::startEast; j < constants::endEast; j++) {
	      			Double_t tot = pico->channelTOT( j );
	      
			        if( tot > constants::minTOT && tot < constants::maxTOT) 
			        	tots[j].push_back(tot);
	    		}

	  		}

		} // lopp events 	


		// loop through the channels and determine binning
		for(Int_t i=0; i<constants::nChannels; i++) {
	      
	    	Int_t size = tots[i].size();
	      	cout << "[calib.binTOT] Channel[ " << i << " ] : " << size << " hits" << endl;
	      	
	      	if( size < numTOTBins * 3 ) { // check for dead channels
	        	
	        	Double_t step = ( constants::maxTOT - constants::minTOT ) / numTOTBins;

	        	for(Int_t j=0; j <= numTOTBins; j++) {

	                totBins[ i ][ j ] = ( step * j ) + constants::minTOT; 
	        	}
	        	cout  << "[calib.binTOT] VPD Channel [ " << i << " ] is dead! " << "( " << size << " hits)" <<endl;
	        	
	        	// set this detector to dead
	        	deadDetector[ i ] = true;

	      	} else { // channel not dead

	      		deadDetector[ i ] = false;
	      		Int_t step = size / (numTOTBins + 1 ); 
	    
	      		// sort into ascending order
	        	std::sort( tots[i].begin(), tots[i].end());
	        	
	        	totBins[ i ][0] = tots[ i ].at(0);
	        	totBins[ i ][ numTOTBins ] = constants::maxTOT;
	        	
	        	for( Int_t j = 1; j < numTOTBins ; j++) {

	        		double d1 = tots[i].at( step * j );
	        		double d2 = tots[ i ].at( step * (j - 1) );
	        	
	        		totBins[ i ][ j ] = ( d1 + d2 ) / 2.0;
	            	
	        	}	// loop over tot bins

	      } // end channle not dead

	  	} // end loop channles
	  	
		for(Int_t i = 0; i< constants::nChannels; i++) {
			tots[i].clear();
		}	
	}
	cout << "[calib." << __FUNCTION__ << "] completed in " << elapsed() << " seconds " << endl;

}

double calib::getCorrection( int vpdChannel, double tot ){
	
	//if ( currentIteration <= 0 )
	//	return 0;

	/*if ( spline[ vpdChannel ] ){
		return spline[ vpdChannel ]->V( tot );
	}*/
	
	int totBin = binForTOT( vpdChannel, tot ); 
	
	return correction[ vpdChannel ][ totBin ];

}

int calib::binForTOT( int vpdChannel, double tot ){

	stringstream sstr; 
	sstr << "channel" << vpdChannel;
	string old = book->cd( sstr.str() );
	sstr.str("");    	sstr << "it" << (currentIteration - 1) <<  "totcor";	
	TH1D* tmp = (TH1D*)book->get( sstr.str() );
	int bin = 0;
	if ( tmp ){
		bin = tmp->GetXaxis()->FindBin( tot );
	} else {
		//cout << "[calib." << __FUNCTION__ << "] Cant Find Tot Bin for tot = " << tot << " in channel : " << vpdChannel << endl;
	}
	book->cd( old );

	return bin;

}

void calib::zVtxPairs(){


	startTimer();
	Int_t nevents = (int)_chain->GetEntries();
	cout << "[calib." << __FUNCTION__ << "[" << currentIteration << "]] " << " Start : " << nevents << endl;

	stringstream sstr;
	book->cd( "vtxPairs");

	sstr.str("");	sstr << "it" << currentIteration << "TPCvsVPD";
	book->make2D( sstr.str(), "TPC vs VPD zVertex", 200, -200, 200, 200, -200, 200 );

	sstr.str(""); 	sstr << "it" << currentIteration << "sumTPCvsVPD";
	book->make2D( sstr.str(), 	"TPC vs VPD zVertex", 	200, -200, 200, 200, -200, 200 );

	sstr.str(""); 	sstr << "it" << currentIteration << "sumTPCminusVPD";
	book->make1D( sstr.str(), "TPC - VPD zVertex", 	600, -200, 200 );
	
	sstr.str(""); 	sstr << "it" << currentIteration << "TPCminusVPD";
	book->make1D( sstr.str(), 	"TPC - VPD zVertex", 	600, -200, 200 );

	sstr.str(""); 	sstr << "it" << currentIteration << "offset";
	book->make2D( sstr.str(), 	"offset", constants::nChannels-1, 1, constants::nChannels, 200, -100, 100 );

	sstr.str(""); 	sstr << "it" << currentIteration << "test";
	book->make1D( sstr.str(), 	"TPC - VPD zVertex", 	600, -200, 200 );

	// loop over all events
	for(Int_t i=0; i<nevents; i++) {
    	_chain->GetEntry(i);

    	sstr.str("");	sstr << "it" << currentIteration << "test";
		book->fill( sstr.str(), 0 );

    	// progress indicator
    	double progress =  ((double)i / (double)nevents);
    	if ( i == nevents - 1)
    		progress = 1.001;
    	if ( i == 0 || (i % (int)(nevents / 150 )) == 0 || i == nevents - 1  ){  
			progressBar( progress, 50 );
		}
		// progress indicator

    	double reference = pico->vpdLeWest[0];
    	double tpcZ = pico->vertexZ;
    	if ( pico->nTofHits <= 1 ) continue;
    	if ( TMath::Abs( tpcZ ) > 100 ) continue;



    	double sumEast = 0;
    	double sumWest = 0;
    	double countEast = 0;
    	double countWest = 0;

    	double corWest;
    	for ( uint j = constants::startWest; j < constants::endWest; j++ ){
    		if ( deadDetector[ j ] ) continue;
    		if ( pico->numHits( j ) < constants::minHits ) continue;
    		double tdcWest = pico->channelTDC( j );
		    double totWest = pico->channelTOT( j );

		    tdcWest -= this->initialOffsets[ j ];
		   
		    if( totWest <= constants::minTOT || totWest > constants::maxTOT) continue;

		    double corWest = getCorrection( j, totWest );
		    tdcWest -= corWest;


		    sumWest += tdcWest;
		    countWest++;

    		for ( uint k = constants::startEast; k < constants::endEast; k++ ){
    			if ( deadDetector[ k ] ) continue;
    			if ( pico->numHits( k ) < constants::minHits ) continue;
				double tdcEast = pico->channelTDC( k );
		    	double totEast = pico->channelTOT( k );

		    	tdcEast -= this->initialOffsets[ k ];


		    	if( totEast <= constants::minTOT || totEast > constants::maxTOT) continue;

		    	double corEast = getCorrection( k, totEast );
		    	tdcEast -= corEast;

		    	if ( j == constants::startWest ){

		    		sstr.str(""); sstr << "it" << currentIteration << "offset";
		    		book->fill( sstr.str(), k, tdcEast - reference );

		    		sumEast += tdcEast;
		    		countEast++;
		    	}

		    	double vpdZ = (constants::c * ( tdcEast - tdcWest)) / 2.0;

		    	sstr.str("");	sstr << "it" << currentIteration << "TPCvsVPD";
		    	book->fill( sstr.str(), tpcZ, vpdZ );
		    	
		    	sstr.str("");	sstr << "it" << currentIteration << "TPCminusVPD";
		    	book->fill( sstr.str(), (tpcZ - vpdZ ) );   		

    		}

    		sstr.str(""); sstr << currentIteration << "offset";
    		book->fill( sstr.str(), j, tdcWest - reference );

    	} // loop west

    	if ( countEast >= 1 && countWest >= 1 ){
    		double vpdZ = constants::c * ( (sumEast / countEast) - (sumWest / countWest) ) / 2.0;

    		sstr.str("");	sstr << "it" << currentIteration << "sumTPCvsVPD";
    		book->fill( sstr.str(), tpcZ, (vpdZ ) ); 

    		sstr.str("");	sstr << "it" << currentIteration << "sumTPCminusVPD";
    		book->fill( sstr.str(), (tpcZ - vpdZ) );
    	}

    }

    cout << "[calib." << __FUNCTION__ << "[" << currentIteration << "]] " << " completed in " << elapsed() << " seconds " << endl;

}

void calib::outlierRejection( bool reject ) {
	// must be called from inside event loop in the calib step

	if ( reject == false ){
		// reset the state
		for ( uint j = constants::startWest; j < constants::endEast; j++ ){
			useDetector[ j ] = true;
		}
		westIsGood = true;
		eastIsGood = true;
		return;
	}

	// get the TPC z vertex
	double tpcZ = pico->vertexZ;

	double vzCut = 40;
	if ( currentIteration == 0 )
		vzCut = 40;
	else if ( currentIteration == 1 )
		vzCut = 20;
	else if ( currentIteration == 2 )
		vzCut = 15;
	else if ( currentIteration == 3 )
		vzCut = 8;
	else if ( currentIteration >= 4 )
		vzCut = 5;

	

	uint numValidPairs = 0;

	// reset the state
	for ( uint j = constants::startWest; j < constants::endWest; j++ ){
		useDetector[ j ] = false;
	}

	eastIsGood = false;
	westIsGood = false;

	double sumEast = 0;
	double sumWest = 0;
	double countEast = 0;
	double countWest = 0;
	stringstream sstr;

	for ( uint j = constants::startWest; j < constants::endWest; j++ ){

		if ( deadDetector[ j ] ) continue;

		double tdcWest = pico->channelTDC( j );
	    double totWest = pico->channelTOT( j );

	    tdcWest -= this->initialOffsets[ j ];

	  
	    if( totWest <= constants::minTOT || totWest > constants::maxTOT) continue;
	    
	    double corWest = getCorrection( j, totWest );
	    tdcWest -= corWest;

	    sumWest += tdcWest;
	    countWest++;

		for ( uint k = constants::startEast; k < constants::endEast; k++ ){
			
			if ( deadDetector[ k ] ) continue;

			double tdcEast = pico->channelTDC( k );
	    	double totEast = pico->channelTOT( k );

	    	tdcEast -= this->initialOffsets[ k ];

	    	if( totEast <= constants::minTOT || totEast > constants::maxTOT) continue;
	    	
	    	double corEast = getCorrection( k, totEast );
	    	tdcEast -= corEast;

	    	if ( j == constants::startWest ){
	    		sumEast += tdcEast;
	    		countEast++;
	    	}

	    	// calculate the VPD z Vertex
	    	double vpdZ = constants::c * ( tdcEast - tdcWest) / 2.0;

	    	sstr.str(""); 		sstr << "it" << currentIteration <<  "All";
	    	book->get( sstr.str(), "OutlierRejection" )->Fill( tpcZ - vpdZ );

	    	if ( TMath::Abs( tpcZ - vpdZ ) < vzCut  ){

	    		// valid pair
	    		useDetector[ k ] = true;
	    		useDetector[ j ] = true;
	    		eastIsGood = true;
	    		westIsGood = true;
	    		numValidPairs ++ ;

	    	} 
		    	  			
		}
	}

	sstr.str(""); 		sstr << "it" << currentIteration <<  "nValidPairs";
	book->get( sstr.str(), "OutlierRejection" )->Fill( numValidPairs );

	uint nAccepted = 0;
	for ( uint j = constants::startWest; j < constants::endWest; j++ ){
		if( useDetector[ j ] )
			nAccepted ++;
	}

	sstr.str(""); 		sstr << "it" << currentIteration <<  "nAcceptedWest";
	book->get( sstr.str(), "OutlierRejection" )->Fill( nAccepted );

	nAccepted = 0;
	for ( uint j = constants::startEast; j < constants::endEast; j++ ){
		if( useDetector[ j ] )
			nAccepted ++;
	}

	sstr.str(""); 		sstr << "it" << currentIteration <<  "nAcceptedEast";
	book->get( sstr.str(), "OutlierRejection" )->Fill( nAccepted );


	//cout << "[calib." << __FUNCTION__ << "] Num Accepted detectors: " << nAccepted << endl;

}

void calib::prepareStepHistograms() {
	/*
	* check that our histos are made for this iteration
	*/
	if ( book->get( "correctedOffsets", "initialOffset"  ) == 0){
		book->cd( "initialOffset" );
		book->make2D( "correctedOffsets", "corrected Initial Offsets", constants::nChannels-1, 1, constants::nChannels, 200, -100, 100 );
	}
	stringstream sstr;
	stringstream sstr2;
	
	for ( int ch = constants::startWest; ch < constants::endEast; ch++ ){
		
		/*
		* cd into the directory for this channel
		*/
		
		sstr.str("");

		sstr << "channel" << ch;
		book->cd( sstr.str() );

		sstr.str("");  	sstr << "it" << currentIteration <<  "tdctot";
    	sstr2 << "Channel " << ch << ";tot; tdc"; 
		TH2D* tmp = new TH2D( 	sstr.str().c_str(), sstr2.str().c_str(), 
								numTOTBins , totBins[ ch ], 
								1000, -20, 20 
							);
		book->add( (char*)sstr.str().c_str(), tmp );

		sstr.str("");
		sstr2.str("");
    	sstr << "it" << currentIteration <<  "tdccor";
    	sstr2 << "Channel " << ch << ";tot; tdc"; 
		tmp = new TH2D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							numTOTBins , totBins[ ch ], 
							1000, -20, 20 
						);
		book->add( (char*)sstr.str().c_str(), tmp );

		
		
    	sstr.str(""); 		sstr << "it" << currentIteration <<  "tdc";
    	sstr2.str(""); 		sstr2 << "Channel " << ch << "; tdc"; 
		TH1D* tmp2 = new TH1D( 	sstr.str().c_str(), sstr2.str().c_str(), 
								500, -10, 10 
							);
		book->add( (char*)sstr.str().c_str(), tmp2 );
		
		
		sstr.str("");		sstr << "it" << currentIteration <<  "avgN";
		sstr2.str("");		sstr2 << "Channel " << ch << " : 1 - <N>;N; tdc";
		tmp = new TH2D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							constants::nChannels-1, 1, constants::nChannels, 
							1000, -20, 20 
						);
		book->add( (char*)sstr.str().c_str(), tmp );

		sstr.str("");		sstr << "it" << currentIteration <<  "cutAvgN";
		sstr2.str("");		sstr2 << "Cut Channel " << ch << " : 1 - <N>;N; tdc";
		tmp = new TH2D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							constants::nChannels-1, 1, constants::nChannels, 
							1000, -20, 20 
						);
		book->add( (char*)sstr.str().c_str(), tmp );

		sstr.str("");		sstr << "it" << currentIteration <<  "avgNB";
		sstr2.str("");		sstr2 << "Channel " << ch << " : 1 - <N>;N; tdc";
		tmp = new TH2D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							constants::nChannels-1, 1, constants::nChannels, 
							1000, -20, 20 
						);
		book->add( (char*)sstr.str().c_str(), tmp );

		sstr.str("");		sstr << "it" << currentIteration <<  "cutAvgNB";
		sstr2.str("");		sstr2 << "Cut Channel " << ch << " : 1 - <N>;N; tdc";
		tmp = new TH2D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							constants::nChannels-1, 1, constants::nChannels, 
							1000, -20, 20 
						);
		book->add( (char*)sstr.str().c_str(), tmp );
		
	}

	/*
	* outlier rejection histos
	*/
	book->cd( "OutlierRejection" );
	sstr.str(""); 		sstr << "it" << currentIteration <<  "All";
	sstr2.str(""); 		sstr2 << "Outlier Rejection: iteration " << currentIteration; 
	TH1D* tmp2 = new TH1D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							600, -200, 200 );
	book->add( sstr.str(), tmp2 );

	sstr.str(""); 		sstr << "it" << currentIteration <<  "nValidPairs";
	sstr2.str(""); 		sstr2 << "Number Of Valid Pairs: iteration " << currentIteration; 
	tmp2 = new TH1D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							49, 0, 50 );
	book->add( sstr.str(), tmp2 );

	sstr.str(""); 		sstr << "it" << currentIteration <<  "nAcceptedWest";
	sstr2.str(""); 		sstr2 << "Number Of Accepted Detectors: iteration " << currentIteration; 
	tmp2 = new TH1D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							39, 0, 40 );
	book->add( sstr.str(), tmp2 );

	sstr.str(""); 		sstr << "it" << currentIteration <<  "nAcceptedEast";
	sstr2.str(""); 		sstr2 << "Number Of Accepted Detectors: iteration " << currentIteration; 
	tmp2 = new TH1D( 	sstr.str().c_str(), sstr2.str().c_str(), 
							39, 0, 40 );
	book->add( sstr.str(), tmp2 );
	/*
	* outlier rejection histos
	*/


	/*
	* check that our histos are made
	*/

	cout << "[calib." << __FUNCTION__ << "[" << currentIteration << "]] " << " Histograms Booked " << endl;

}

void calib::step( ) {

	cout << "[calib." << __FUNCTION__ << "[" << currentIteration << "]] " << " Start " << endl;
	
	startTimer();

	bool outliers =  config.getAsBool( "outlierRejection" );
	bool removeOffset = config.getAsBool( "removeOffset" );

	double outlierCut = 2;
	if ( currentIteration == 1)
		outlierCut = 1;
	else if ( currentIteration >= 2 )
		outlierCut = 0.6;
	
	// the data we will use over and over 
	double tot[ constants::nChannels ];		// tot value
	double tdc[ constants::nChannels ];		// tdc value
	double off[ constants::nChannels ];		// offset value
	double tAll[ constants::nChannels ];	// tdc with all corrections ( offset and correction)

	// correction based on channel and tot value
	double corr[ constants::nChannels ];	
	// reference tdc time => the 1st channel on the west side
	double reference;
	stringstream sstr;

	// make sure the histograms are ready
	prepareStepHistograms();
	

	Int_t nevents = (int)_chain->GetEntries();
	for(Int_t i = 0; i < nevents; i++) {
    	_chain->GetEntry(i);

    	// progress indicator
    	double progress =  ((double)i / (double)nevents);
    	if ( i == nevents - 1)
    		progress = 1.001;
    	if ( i == 0 || (i % (int)(nevents / 150 )) == 0 || i == nevents - 1  ){  
			progressBar( progress, 50 );
		}
		// progress indicator

    	// perform outlier rejection for this event
    	outlierRejection( outliers );

    	

    	float vx = pico->vertexX;
    	float vy = pico->vertexY;
    	float vxy = TMath::Sqrt( vx*vx + vy*vy );
    	if ( vxy > 1 ) continue;

    	double tpcZ = pico->vertexZ;
    	if ( pico->nTofHits <= 1 ) continue;
    	if ( TMath::Abs( tpcZ ) > 100 ) continue;

    	averageN();
 		   	

    	// Alias the values for this event for ease
    	for( int j = constants::startWest; j < constants::endEast; j++) {
    		
    		if ( deadDetector[ j ] ) continue;
			if ( !useDetector[ j ] ) continue;

    		tot[ j ] = pico->channelTOT( j );
    		tdc[ j ] = pico->channelTDC( j );
    		off[ j ] = this->initialOffsets[ j ];
    		if ( removeOffset )
    			tAll[ j ] = tdc[ j ] - off[ j ];
    		
    		if(tot[ j ] <= constants::minTOT || tot[ j ] > constants::maxTOT) continue;
  			
  			corr[ j ] = getCorrection( j, tot[ j ] );
  			tAll[ j ] -= corr[ j ];
    	}
    	reference = pico->vpdLeWest[0];

		// loop over every channel on the west and then on the east side
		for( int j = constants::startWest; j < constants::endEast; j++) {
			
			// skip dead detectors
			if ( deadDetector[ j ] ) continue;
			if ( !useDetector[ j ] ) continue;
			// require the tot is within range
	    	if(tot[ j ] <= constants::minTOT || tot[ j ] > constants::maxTOT) continue;


	    	double tdcSumWest = 0;
			double tdcSumEast = 0;
	    	double countEast = 0;
	    	double countWest = 0;

	    	for( int k = constants::startWest; k < constants::endEast; k++) {

	    		// skip dead detectors
				if ( deadDetector[ k ] ) continue;
				if ( !useDetector[ k ] ) continue;
	    		
	    		//if(tot[ k ] <= constants::minTOT || tot[ k ] > constants::maxTOT) continue;
	    		if ( j == k ) continue;
	    		
	    		if ( k >= constants::startWest && k < constants::endWest ){
	    			tdcSumWest += ( tAll[ k ]);
	    			countWest ++;
	    		} else if ( k >= constants::startEast && k < constants::endEast ){
	    			tdcSumEast += ( tAll[ k ]);
	    			countEast ++;
	    		}

			}	// loop on vpdChannel k

			/*
			*	Now recalculate the average times using the previously calculated average to
			*	apply a cut on the range of variation
			*/
			double cutSumWest = 0;
			double cutSumEast = 0;
	    	double cutCountEast = 0;
	    	double cutCountWest = 0;

	    	for( int k = constants::startWest; k < constants::endEast; k++) {

	    		// skip dead detectors
				if ( deadDetector[ k ] ) continue;
				if ( !useDetector[ k ] ) continue;
	    		if ( j == k ) continue;
	    		
	    		if ( k >= constants::startWest && k < constants::endWest ){
	    			double tAvg = tdcSumWest / countWest;
	    			if ( tAll[ k ] - tAvg < outlierCut && tAll[ k ] - tAvg > -outlierCut ){
	    				cutSumWest += tAll[ k ];
	    				cutCountWest ++;
	    			}
	    		} else if ( k >= constants::startEast && k < constants::endEast ){
	    			double tAvg = tdcSumEast / countEast;
	    			if ( tAll[ k ] - tAvg < outlierCut && tAll[ k ] - tAvg > -outlierCut ){
	    				cutSumEast += tAll[ k ];
	    				cutCountEast ++;
	    			}
	    		}

			}	// loop on vpdChannel k


	 		if ( currentIteration == 0 ){

	 			/*
	 			*	Plot the offsets after correction just to be sure it all works
	 			*/
	 			book->cd( "initialOffset" );
		    	book->fill( "correctedOffsets", j, tdc[ j ] - reference - off[ j ] );
		    }

		    // set the avg and count varaibles for this run
		    // if j corresponds to a west channel then use tdcSumWest, countWest
		    // if j corresponds to an east channel then use tdcSumEast, countEast
	    	double avg = (tdcSumWest / countWest );
	    	double cutAvg = ( cutSumWest / cutCountWest );
	    	int count = countWest;
	    	int cutCount = cutCountWest;

	    	int start  	= constants::startWest;
			int end 	= constants::endWest;

	 		if ( j >= constants::startEast && j < constants::endEast ){
	    		avg = (tdcSumEast / countEast );
	    		count = countEast;
	    		cutAvg = ( cutSumEast / cutCountEast );
	    		cutCount = cutCountEast;
	    		start  	= constants::startEast;
				end 	= constants::endEast;
	    	}


	    	// change into this channels dir for histogram saving
			sstr.str("");		sstr << "channel" << j;
			book->cd( sstr.str() );

			if ( true == eastIsGood && true == westIsGood) {
		    	for( int k = start; k < end; k++) {

		    		// skip dead detectors
					if ( deadDetector[ k ] ) continue;
					if ( !useDetector[ k ] ) continue;
		    		if ( j == k ) continue;
		    		if ( avg == 0 ) continue;

		    		sstr.str("");	sstr << "it" << currentIteration <<  "avgN";
		    		book->fill( sstr.str(), count, tAll[ k ] - avg );

		    		if ( count <= 0 ) continue;

		    		

				}	// loop on vpdChannel k
			}

			sstr.str("");	sstr << "it" << currentIteration <<  "cutAvgN";
		    book->fill( sstr.str(), count, tAll[ j ] - cutAvg );

	    	if ( count <= constants::minHits ) continue;

	    	sstr.str("");	sstr << "it" << currentIteration <<  "tdctot" ;
	    	book->fill( sstr.str(), tot[ j ], tdc[ j ] - off[ j ] - cutAvg );

	    	sstr.str("");	sstr << "it" << currentIteration <<  "tdccor";
	    	book->fill( sstr.str(), tot[ j ], tAll[ j ] - cutAvg );

	    	sstr.str("");	sstr << "it" << currentIteration <<  "tdc";
	    	book->fill( sstr.str(), tAll[ j ]  - cutAvg );
	    	
		}	
	}

	cout << "[calib." << __FUNCTION__ << "[" << currentIteration << "]] " << " completed in " << elapsed() << " seconds " << endl;
	

	makeCorrections();
	

	currentIteration++;

	
}

/*
*
*	finish
*	called *after* the last iteration to perform final cuts and finish the calibration calculations
*
*/
void calib::finish( ){




}

void calib::loop( ) {

	for ( unsigned int i = 0; i < maxIterations; i++ ){
		step();
		//stepReport();
	}

}


void calib::makeCorrections(){

	cout << "[calib." << __FUNCTION__ << "[" << currentIteration << "]] " << " Start " << endl;
	
	startTimer();

	stringstream sstr;

	bool differential = config.getAsBool( "differential" );
	string hName = "tdctot";

	if ( differential )
		hName = "tdccor";

	// get the corrections for the next iteration 
	for( int k = constants::startWest; k < constants::endEast; k++) {

		// skip dead detectors
		if ( deadDetector[ k ] ) continue;

		// switch into channel dir
		sstr.str("");		sstr << "channel" << k;
		book->cd( sstr.str() );


		sstr.str("");   	sstr << "it" << currentIteration <<  hName;
	    TH2D* tmp = (TH2D*) book->get( sstr.str() );

	    sstr.str("");		sstr << "channel" << k << "/fit";
		book->cd( sstr.str() );

		// do the fit
	    tmp->FitSlicesY();

	    sstr.str("");    	sstr << "it" << currentIteration <<  hName << "_1";
	    TH1D* tmp2 = (TH1D*) gDirectory->FindObject( sstr.str().c_str() );

	    sstr.str("");	sstr << "channel" << k;
		book->cd( sstr.str() );

	    sstr.str("");    	sstr << "it" << currentIteration <<  "totcor";
	    TH1D* cor = (TH1D*) tmp2->Clone( sstr.str().c_str() );
	    book->add( sstr.str(), cor  );

	    vector<double> x( numTOTBins ); 
   		vector<double> y( numTOTBins ); 
	    // histogram bin 0 is underflow, index 1 is the first real bin
	    for ( int ib = 1; ib <= numTOTBins ; ib ++ ){
	    	if ( currentIteration == 0 || !differential )
	    		correction[ k ][ ib  ] = cor->GetBinContent( ib );
	    	else if ( differential && currentIteration >= 1 )
	    		correction[ k ][ ib  ] += cor->GetBinContent( ib );
	    	y[ ib-1 ] = correction[ k ][ ib ];
	    	x[ ib-1 ] = totBins[ k ][ ib - 1 ];
	    }

	    // now make splines
	    spline[ k ] = new ROOT::Math::Interpolator( x, y, ROOT::Math::Interpolation::kAKIMA);
	    can->Divide(1, 1);
	    can->cd(0);

	    sstr.str("");		sstr << "Channel " << k ;
	    cor->SetTitle( sstr.str().c_str() );
	    cor->Draw("");
	    double step = 0.2;
	   	const Int_t n = ( (constants::maxTOT -  constants::minTOT - 5) / step)  + 1;
	   	Int_t i = 0;
	   	Float_t xcoord[n], ycoord[n];
	   	for ( double xi = constants::minTOT+2.5; xi < constants::maxTOT-2.5; xi += step) { 
	      	xcoord[i] = xi;
	      	ycoord[i] = spline[ k ]->Eval(xi);
	      	i++; 
	   	}
	   	TGraph *gr = new TGraph(n,xcoord,ycoord);
   		gr->SetMarkerColor(kRed);
   		gr->SetMarkerStyle(7);
   		gr->Draw("same CP");
	    savePage();

	}

	cout << "[calib." << __FUNCTION__ << "[" << currentIteration << "]] " << " completed in " << elapsed() << " seconds " << endl;

}

void calib::writeParameters(  ){

	string outName = config.getAsString( "baseName" ) + config.getAsString( "paramsOutput" );

	if ( outName.length() <= 4 )
		return;

	ofstream f;
	f.open( outName.c_str() );

	bool removeOffset = config.getAsBool( "removeOffset" );

	for ( int j = constants::startWest; j < constants::endEast; j++ ){

		f << (j + 1) << endl;
		f << numTOTBins << endl;

		for ( int i = 0; i <= numTOTBins; i++ ){
			f << totBins[ j ][ i ] << " ";
		}
		f << endl;
		for ( int i = 0; i <= numTOTBins; i++ ){
			double off = 0;
			if ( removeOffset ){
				off = initialOffsets[ j ];
				if ( j >= constants::startEast && j < constants::endEast )
					off += westMinusEast;
			}

			f << (correction[ j ][ i + 1 ] + off ) << " ";
		}
		f << endl;
	} 

	f.close();

}

void calib::readParameters(  ){

	string inName = config.getAsString( "baseName" ) + config.getAsString( "paramsInput" );

	cout << "[calib." << __FUNCTION__ << "] " << " Start " << endl;
	
	startTimer();

	bool good = true;

	ifstream infile;
    infile.open( inName.c_str() );

	if ( infile.is_open() ){
		for ( int i = 0 ; i < constants::nChannels; i++ ){

			int channel = -1;
			infile >> channel;

			int tBins = 0;
			infile >> tBins;


		
			if ( 	channel >= 1 && channel <= 38 &&
					tBins == numTOTBins ){
				if ( tBins <= 50 )
					tBins++;

				double tmp = 0;
				for ( int i=0; i < tBins; i++ ){
					infile >> tmp;
					totBins[ channel - 1 ][ i ] = tmp;
				}
				
				for ( int i=0; i < tBins; i++ ){
					infile >> tmp;
					correction[ channel - 1 ][ i ] = tmp;
				}
				
			} else {
				cout << "[calib." << __FUNCTION__ <<  "] " << "Bad file format, cannot load parameters" << endl;
				good = false;

			}


		}
	}

	infile.close();

	if ( good ){
		cout << "[calib." << __FUNCTION__ <<  "] " << " Read parameters for all channels " << endl;
		currentIteration = 5;
	}

	cout << "[calib." << __FUNCTION__ <<  "] " << " completed in " << elapsed() << " seconds " << endl;
}

void calib::stepReport() {


}



void calib::averageN() {


	double outlierCut = 2;
	if ( currentIteration == 1)
		outlierCut = 1;
	else if ( currentIteration >= 2 )
		outlierCut = 0.6;
	
	// the data we will use over and over 
	double tot[ constants::nChannels ];		// tot value
	double tdc[ constants::nChannels ];		// tdc value
	double off[ constants::nChannels ];		// offset value
	double tAll[ constants::nChannels ];	// tdc with all corrections ( offset and correction)

	// correction based on channel and tot value
	double corr[ constants::nChannels ];	
	// reference tdc time => the 1st channel on the west side
	double reference;
	stringstream sstr;


	// Alias the values for this event for ease
	for( int j = constants::startWest; j < constants::endEast; j++) {
		
		if ( deadDetector[ j ] ) continue;
		if ( !useDetector[ j ] ) continue;

		tot[ j ] = pico->channelTOT( j );
		tdc[ j ] = pico->channelTDC( j );
		off[ j ] = this->initialOffsets[ j ];	
		tAll[ j ] = tdc[ j ] - off[ j ];
		
		if(tot[ j ] <= constants::minTOT || tot[ j ] > constants::maxTOT) continue;
			
			corr[ j ] = getCorrection( j, tot[ j ] );
			tAll[ j ] -= corr[ j ];
	}
	reference = pico->vpdLeWest[0];

	
	for ( int i = constants::startWest; i < constants::endWest; i++ ){

		sstr.str("");		sstr << "channel" << i;
		book->cd( sstr.str() );

		double count = 0;
		double avg = 0;
		double c = 0, a = 0;

		if ( !useDetector[ i ] ) continue;

		if ( westIsGood && eastIsGood ){

			for ( int j = constants::startWest; j < constants::endWest; j++ ){
				if ( useDetector[ j ] && i != j ){
					++c;
					a += tAll[ j ];
				}
			}
			


			if ( c >0)
				a /= c;
			else 
				a = -9999;

			
			if ( c > 0 ){
				for ( int j = constants::startWest; j < constants::endWest; j++ ){
					if ( useDetector[ j ] && i != j ){
						sstr.str("");		sstr << "it" << currentIteration <<  "avgNB";
						book->get( sstr.str() )->Fill( c, tAll[ j ] - a );
					}
				}
			}
			
			for ( int j = constants::startWest; j < constants::endWest; j++ ){
				if ( useDetector[ j ] && i != j ){
					if ( a > -9990 ){
						
						if ( 	tAll[ j ] - a > -outlierCut && 
								tAll[ j ] - a < outlierCut ){
							++count;
							avg += tAll[ j ];
						}
					}
				}
			}

			if ( count )
				avg /= count;
			else
				avg = -9999;

			if ( count ){
				sstr.str("");		sstr << "it" << currentIteration <<  "cutAvgNB";
				book->get( sstr.str() )->Fill( count, tAll[ i ] - avg );
			}

		} // West and East Good


	}
}

void calib::savePage(){
	can->Print( ( config.getAsString( "baseName" ) + config.getAsString( "reportOutput" ) ).c_str() );
}


Double_t calib::detectorResolution(Double_t *x, Double_t *par){
	Double_t resval = 0.0;
	if (x[0]>0){
		resval	= TMath::Sqrt( x[0] / ( 1.0 + x[0] ) );
		return ( par[0] / resval );
	} else {
		return 0.0;
	}
}