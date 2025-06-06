/* 
 * main_wilson_lines_correlator_stacked.cu                                                               
 * 
 * Rasmus Larsen, 25 Feb 2021
 * 
 */

#include "../simulateqcd.h"
#include "../modules/observables/wilsonLineCorrelatorMultiGPU.h"
#include "../modules/gradientFlow/gradientFlow.h"


//#include <iostream>
//using namespace std;

#define PREC double
#define STACKS 32 


template<class floatT>
struct WLParam : LatticeParameters {
    Parameter<floatT>      gtolerance;
    Parameter<int,1>       maxgfsteps;
    Parameter<int,1>       numunit;
    Parameter<int> load_conf;
    Parameter <std::string> gauge_file;
    Parameter <std::string> directory;
    Parameter <std::string> file_type;

    Parameter<floatT>  wilson_step;
    Parameter<floatT> wilson_start;
    Parameter<floatT> wilson_stop;
    Parameter<int,1> use_wilson;

    Parameter<int,1>       cutRadius;
    Parameter<int,1>       useInfoFile;


    WLParam() {
        addDefault (gtolerance,"gtolerance",1e-6);
        addDefault (maxgfsteps,"maxgfsteps",9000);
        addDefault (numunit   ,"numunit"   ,20);
        addDefault(load_conf, "load_conf", 0);
        addOptional(gauge_file, "gauge_file");
        add(directory, "directory");
        add(file_type, "file_type");

	addDefault (use_wilson,"use_wilson",0);
	addDefault (wilson_step,"wilson_step",0.0);
	addDefault (wilson_start,"wilson_start",0.0);
	addDefault (wilson_stop,"wilson_stop",0.0);
        addDefault(cutRadius, "cutRadius", 100000);
        addDefault(useInfoFile, "useInfoFile", 1);
    }
};

template<class floatT>
struct milcInfo : ParameterList {
    Parameter<floatT>  ssplaq;
    Parameter<floatT>  stplaq;
    Parameter<floatT>  linktr;

    milcInfo() {
        add(ssplaq, "gauge.ssplaq");
        add(stplaq, "gauge.stplaq");
        add(linktr, "gauge.nersc_linktr");
    }
};


int main(int argc, char *argv[]) {

    /// Controls whether DEBUG statements are shown as it runs; could also set to INFO, which is less verbose.
    stdLogger.setVerbosity(INFO);

    /// Initialize parameter class.
    WLParam<PREC> param;

    /// Initialize the CommunicationBase.
    CommunicationBase commBase(&argc, &argv);

    param.readfile(commBase, "../parameter/tests/wilsonCorrelatorTest.param", argc, argv);


    commBase.init(param.nodeDim());

//    cout << param.nodeDim[0] << " param 0 " <<  param.nodeDim[1] << " param 1 " << param.nodeDim[2] << " param 2 " << param.nodeDim[3] << " param 3 " <<endl; 

    /// Set the HaloDepth.
    const size_t HaloDepth = 2;

    rootLogger.info("Initialize Lattice");

    /// Initialize the Lattice class.
    initIndexer(HaloDepth,param,commBase);

    /// Initialize the Gaugefield.
    rootLogger.info("Initialize Gaugefield");
    Gaugefield<PREC,true,HaloDepth> gauge(commBase);

    /// Initialize gaugefield with unit-matrices.
    gauge.one();


////// gauge read in rutine to be replaced with random generated
/*
    std::string gauge_file;

    // load gauge file, 0 start from 1, 1 and 2 load file, 2 will also gauge fix
    if (param.load_conf() == 0)
    {
        rootLogger.info("Starting from unit configuration");
        gauge.one();
    }
    else if(param.load_conf() == 2 || param.load_conf() == 1)
    {
        std::string file_path = param.directory();
        file_path.append(param.gauge_file()); 
        rootLogger.info("Starting from configuration: ", file_path);
//	rootLogger.info() << param.gauge_file() << endl;
        if(param.file_type() == "nersc"){
            gauge.readconf_nersc(file_path);
        }
        else if(param.file_type() == "milc"){
            gauge.readconf_milc(file_path);

            gauge.updateAll();         
            GaugeAction<PREC,true,HaloDepth> enDensity(gauge);
            PREC SpatialPlaq  = enDensity.plaquetteSS();
            PREC TemporalPlaq = enDensity.plaquette()*2.0-SpatialPlaq;
            rootLogger.info( "plaquetteST: "   , TemporalPlaq);
            rootLogger.info( "plaquetteSS: " , SpatialPlaq);


            if(param.useInfoFile()){
                std::string info_path = file_path;
                info_path.append(".info");
                milcInfo<PREC> paramMilc;
                paramMilc.readfile(commBase,info_path);
                rootLogger.info( "plaquette SS info file: " ,  (paramMilc.ssplaq())/3.0  );
                rootLogger.info( "plaquette ST info file: " ,  (paramMilc.stplaq())/3.0  );
                rootLogger.info( "linktr info file: " , paramMilc.linktr()  );
                if(abs((paramMilc.ssplaq())/3.0-SpatialPlaq) > 1e-5){
                    throw std::runtime_error(stdLogger.fatal("Error ssplaq!"));
                }
                if(abs((paramMilc.stplaq())/3.0-TemporalPlaq) > 1e-5){
                    throw std::runtime_error(stdLogger.fatal("Error stplaq!"));
                }
            }

        }
    }

*/


// random generated gauge file

   grnd_state<false> h_rand;
   grnd_state<true> d_rand;
   h_rand.make_rng_state(12345);
   d_rand = h_rand;
   gauge.random(d_rand.state);



    /// Exchange Halos
    gauge.updateAll();

   

    /// Initialize ReductionBase.
    LatticeContainer<true,PREC> redBase(commBase);

    /// We need to tell the Reductionbase how large our array will be. Again it runs on the spacelike volume only,
    /// so make sure you adjust this parameter accordingly, so that you don't waste memory.
    typedef GIndexer<All,HaloDepth> GInd;
    redBase.adjustSize(GInd::getLatData().vol4);
    rootLogger.info( "volume size " , GInd::getLatData().globvol4  );


    if(param.use_wilson()){
        rootLogger.info( "Start Wilson Flow"  );

        std::vector<PREC> flowTimes = {100000.0};
        PREC start = param.wilson_start();
        PREC stop  = param.wilson_stop();
        PREC step_size = param.wilson_step();
        const auto force = static_cast<Force>(static_cast<int>(0));
        gradientFlow<PREC, HaloDepth, fixed_stepsize,force> gradFlow(gauge,step_size,start,stop,flowTimes,0.0001);

        bool continueFlow =  gradFlow.continueFlow();
//	rootLogger.info() << "step " << gradFlow._step_size;
//	rootLogger.info() << "continueFlow " << continueFlow;
//	rootLogger.info() << "step " << gradFlow._step_size;
        while (continueFlow) {
            gradFlow.updateFlow();
//            rootLogger.info() << "step " << gradFlow._step_size;
            continueFlow = gradFlow.continueFlow(); //! check if the max flow time has been reached
//	    rootLogger.info() << "step " << gradFlow._step_size;
//	    gradFlow.updateFlow();
	}

        gauge.updateAll();

        rootLogger.info( "End Wilson Flow"  );
    }

///////////// gauge fixing

    if(param.load_conf() ==2){
        GaugeFixing<PREC,true,HaloDepth>    GFixing(gauge); 
        int ngfstep=0;
        PREC gftheta=1e10;
        const PREC gtol = param.gtolerance();        //1e-6;          /// When theta falls below this number, stop...
        const int ngfstepMAX = param.maxgfsteps() ;  //9000;     /// ...or stop after a fixed number of steps; this way the program doesn't get stuck.
        const int nunit= param.numunit();            //20;            /// Re-unitarize every 20 steps.
        while ( (ngfstep<ngfstepMAX) && (gftheta>gtol) ) {
            /// Compute starting GF functional and update the lattice.
            GFixing.gaugefixOR();
            /// Due to the nature of the update, we have to re-unitarize every so often.
            if ( (ngfstep%nunit) == 0 ) {
                 gauge.su3latunitarize();
            }
            /// Re-calculate theta to determine whether we are sufficiently fixed.
            gftheta=GFixing.getTheta();
            ngfstep+=1;
//	    rootLogger.info() << "gftheta = " << gftheta;
        }
        gauge.su3latunitarize(); /// One final re-unitarization.

        rootLogger.info( "Gauge fixing finished in " , ngfstep , " steps, with gftheta = " , gftheta );
    }
   



    rootLogger.info( "start wilson correlator" );

         StopWatch<true> timer;

    /// Start timer.
    timer.start();

    /// Exchange Halos
    gauge.updateAll();

    PREC dot;

     WilsonLineCorrelatorMultiGPU<PREC,HaloDepth,STACKS> WilsonClass;

    std::vector<PREC> dotVector;
    PREC * results;
    results = new PREC[(GInd::getLatData().globvol3/2+GInd::getLatData().globLX*GInd::getLatData().globLY)*GInd::getLatData().globLT];
    ///  
    timer.start();
    //// loop over length of wilson lines
    for(int length = 1; length<GInd::getLatData().globLT+1;length++){

        /// calculate the wilson line starting from any spacetime point save in mu=0 direction
        WilsonClass.gWilson(gauge, length);

        /// copy from mu=0 to mu=1
        gauge.template iterateOverBulkAtMu<1,64>(CopyFromMu<PREC,HaloDepth,All,0>(gauge));
        gauge.updateAll();

        // initial position x0=-1 due to adding dx in first line
        int x0 = -STACKS;
        int y0 = 0;
        int z0 = 0;

        int dx = STACKS;
        int dy = 1;
        int dz = 1;

        int points = (GInd::getLatData().globvol3/2+GInd::getLatData().globLX*GInd::getLatData().globLY);
        for(size_t i = 0; i<GInd::getLatData().globvol3/2+GInd::getLatData().globLX*GInd::getLatData().globLY;i+=STACKS){
            x0 += dx;

            if(x0 >= (int)GInd::getLatData().globLX || x0 <0){
                dx *= -1;
                x0 += dx;
                y0 += dy;
                if(y0 >= (int)GInd::getLatData().globLY|| y0 <0){
                    dy *= -1;
                    y0 += dy;
                    z0 += dz;
                    /// move mu=1 direction by dz
                    WilsonClass.gMoveOne(gauge,2,dz);
                    gauge.updateAll();
                }
                else if(param.nodeDim[1]>1){
                    /// mode mu=1 direction by dy
                    WilsonClass.gMoveOne(gauge,1,dy);
                    gauge.updateAll();
                }
            }

     ///// cut off
            int ir2 = 0;
            if(y0 > (int)GInd::getLatData().globLY/2){
                ir2 += (y0-(int)GInd::getLatData().globLY)*(y0-(int)GInd::getLatData().globLY);
            }
            else{
                ir2 += y0*y0;
            }

	    if(z0 > (int)GInd::getLatData().globLZ/2){
                ir2 += (z0-(int)GInd::getLatData().globLZ)*(z0-(int)GInd::getLatData().globLZ);
            }
            else{
                ir2 += z0*z0;
            }


            if(ir2 < (param.cutRadius()*param.cutRadius() +1) ){


        // A(x).A(x+r)^dag along direction x and also y if not split on different GPU's
                if(param.nodeDim[1] == 1){
                     dotVector = WilsonClass.gDotAlongXYStackedShared(gauge,y0,redBase);
                }
                else{
                    dotVector = WilsonClass.gDotAlongXYStackedShared(gauge,0,redBase);
                }

                if(dx>0){
                    for(int j = 0;j < STACKS ; j++){
                        results[i+j+points*(length-1)] = dotVector[j];
                    }
                }
                else{
                    for(int j = 0;j < STACKS ; j++){
                       results[i+STACKS-1-j+points*(length-1)] = dotVector[j];
                    }
                }
            


            }
////////////////////
        }
    }


/////////////// Save into radius squared file

    int r2max = 0;
    r2max += GInd::getLatData().globLX*GInd::getLatData().globLX;
    r2max += GInd::getLatData().globLY*GInd::getLatData().globLY;
    r2max += GInd::getLatData().globLZ*GInd::getLatData().globLZ;


    PREC * results_r2 = new PREC[r2max+1];
    PREC * norm_r2 = new PREC[r2max+1];


    for(int length = 1; length<GInd::getLatData().globLT+1;length++){

        for(int ir2=0; ir2<r2max+1; ir2++) {
            results_r2[ir2] = 0.0;
            norm_r2[ir2] = 0.0;
        }



        int x0 = -1;
        int y0 = 0;
        int z0 = 0;

        int dx = 1;
        int dy = 1;
        int dz = 1;

        size_t entries = GInd::getLatData().globvol3/2+GInd::getLatData().globLX*GInd::getLatData().globLY;
        for(size_t i = 0; i<entries;i++){
            x0 += dx;

            if(x0 >= (int)GInd::getLatData().globLX || x0 <0){
                dx *= -1;
                x0 += dx;
                y0 += dy;
                if(y0 >= (int)GInd::getLatData().globLY|| y0 <0){
                    dy *= -1;
                    y0 += dy;
                    z0 += dz;
                }
            }

            dot = results[i+entries*(length-1)];

            // save results
           

                int ir2 = 0;
                if(x0 > (int)GInd::getLatData().globLX/2){
                    ir2 += (x0-(int)GInd::getLatData().globLX)*(x0-(int)GInd::getLatData().globLX);
                }
                else{
                    ir2 += x0*x0;
                }

                if(y0 > (int)GInd::getLatData().globLY/2){
                    ir2 += (y0-(int)GInd::getLatData().globLY)*(y0-(int)GInd::getLatData().globLY);
                }
                else{
                    ir2 += y0*y0;
                }

                if(z0 > (int)GInd::getLatData().globLZ/2){
                    ir2 += (z0-(int)GInd::getLatData().globLZ)*(z0-(int)GInd::getLatData().globLZ);
                }
                else{
                    ir2 += z0*z0;
                }

                if(ir2 < (param.cutRadius()*param.cutRadius() +1) ){

                    // factor for counting contributions
                    // Initial factor 2 for symmetry between z and -z
                    // double the factor if x or y = l/2 due to periodicity
                    double factor = 1.0;

                    if(z0 == (int)GInd::getLatData().globLZ/2 || z0 == 0){
                        factor = 0.5*factor;
                        if((y0 == (int)GInd::getLatData().globLY/2 || y0 == 0) && (x0 == (int)GInd::getLatData().globLX/2 || x0==0) ){
                            factor = 2.0*factor;
                        }
                    }

                    if( (z0 == (int)GInd::getLatData().globLZ/2 || z0==0) && (y0 == (int)GInd::getLatData().globLY/2 || y0 == 0) && (x0 == (int)GInd::getLatData().globLX/2 || x0==0) ){
                        factor = 0.5*factor;
                    }

                    results_r2[ir2] += factor*dot;
                    norm_r2[ir2] += factor;
               }

        }
/////// write to file

       
    }

/////////////// final check



    PREC sum = 0.0;
    for(size_t i = 0; i<(GInd::getLatData().globvol3/2+GInd::getLatData().globLX*GInd::getLatData().globLY)*GInd::getLatData().globLT;i+=1){
	sum += abs(results[i]);
    }

    timer.stop();
    rootLogger.info( "Time for operators: " , timer  );
    rootLogger.info( "abs(sum) = ",  sum);

    rootLogger.info( "abs(sum)-Known = ",  sum-2356.61222319740476);

    delete [] results;
    delete [] results_r2;
    delete [] norm_r2;

    bool lerror = false;
    if(abs(sum-2356.61222319740476)> 1e-3){
        lerror = true;
    }


    if(lerror) {
        rootLogger.error("At least one test failed!");
        return -1;
    } else {
        rootLogger.info("test passed!" );
    }


    return 0;
}
