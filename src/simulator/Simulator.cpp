/* 
Software License Agreement (BSD License)

Copyright (c) 2013, Julian de Hoog <julian@dehoog.ca>
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided
   with the distribution.
 * The name of the author may not be used to endorse or promote 
   products derived from this software without specific prior 
   written permission from the author.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE. 
*/

#include "Simulator.h"

Simulator::Simulator(Config* config):
        trafficModel(config),
        householdDemand(config),
        spotPrice(config)
{
    std::cout << "Loading Simulator..." << std::endl;
    std::cout.flush();
    
    lastIteration = 0;
    
    // Set parameters
    startTime.set(config->getConfigVar("starttime"));
    currTime = startTime;
    finishTime.set(config->getConfigVar("finishtime"));
    
    showDebug = config->getBool("showdebug");
    generateReport = config->getBool("generatereport");
    simInterval = config->getInt("simulationinterval");
    delay = config->getInt("intervaldelay");
    
    // Create interface to load flow simulator
    std::string modelname = config->getString("modelname");
    switch(config->getLoadFlowSim()) {
        case 0:         loadflow = new TestingInterface(modelname);
                        break;
        case 1:         loadflow = new MatlabInterface(modelname);
                        break;
        default:        loadflow = new MatlabInterface(modelname);
                        break;
    }
    
    // Load model, set initial values
    gridModel.initialise(config, loadflow);
    
    // Assign first vehicle profiles
    trafficModel.initialise(startTime, gridModel.vehicles);

    // Create charging algorithm
    switch(config->getChargingAlg()) {
        case 0:         charger = new ChargingUncontrolled(config, gridModel);
                        break;
        case 1:         charger = new ChargingEqualShares(config, gridModel);
                        break;
        case 2:         charger = new ChargingDistributed(config, gridModel, startTime, householdDemand);
                        break;
        case 3:         charger = new ChargingTOU(config, gridModel);
                        break;
        default:        charger = new ChargingUncontrolled(config, gridModel);
                        break;
    }
    
    // Initialise log
    log.initialise(config, gridModel);
    
    // Print load flow model to log directory
    //loadflow->printModel(log->getDir());
    
    std::cout << " - Simulator loaded OK" << std::endl << std::endl;
}

Simulator::~Simulator() {
}

void Simulator::run() {
    boost::posix_time::ptime time_runStart, time_cycleStart;
    time_runStart = boost::posix_time::microsec_clock::local_time();
    
    while(!currTime.isLaterThan(finishTime)) {
        time_cycleStart = boost::posix_time::microsec_clock::local_time();
        
        std::cout << "-------------------------------------------" << std::endl;        
        currTime.display();
        std::cout << std::endl;

        // Update electricity spot price
        // spotPrice.update(currTime);
        
        // Update traffic model
        trafficModel.update(currTime, gridModel.vehicles);
        
        // Update vehicles' battery SOC based on distance driven / charging
        gridModel.updateVehicleBatteries();

        // Determine EV charging rates
        charger->setChargeRates(currTime, gridModel);

        // Update grid model - generate household loads & apply charge rates
        gridModel.generateLoads(currTime, householdDemand);
                
   
        // Show some results
        if(showDebug) {
            //spotPrice.display();
            trafficModel.displaySummary(gridModel.vehicles);
            gridModel.displayVehicleSummary();
            gridModel.displayLoadSummary();
        }

        // Provide quick update on timing to output
        timingUpdate();

        // Run load flow (typically the bottleneck)
        gridModel.runLoadFlow(currTime);
        
        // Log data
        log.update(currTime, gridModel, charger, spotPrice);
        
        // Add optional user specified delay into cycle
        // while(utility::timediff(boost::posix_time::microsec_clock::local_time(), time_cycleStart) < delay);
        
        // Estimate timing
        lastIteration = long(utility::timeElapsed(time_cycleStart));
        std::cout << "Cycle complete" << std::endl; //, took: " << utility::timeDisplay(lastIteration) << std::endl;

        currTime.increment(simInterval);
        
    }
    
    std::cout << "-------------------------------------------" << std::endl; 
    std::cout << "Simulation complete, took " << utility::timeDisplay(utility::timeElapsed(time_runStart)) << std::endl;
    std::cout << "Generating report ..." << std::endl; 
    
    if(generateReport)
        loadflow->generateReport(log.getDir(), currTime.month, currTime.isWeekday(), simInterval);
    
    std::cout << "Report written to " << log.getDir() << std::endl; 
    std::cout << "-------------------------------------------" << std::endl; 
}


void Simulator::timingUpdate() {
    long total = finishTime.minus(startTime, simInterval);
    long complete = currTime.minus(startTime, simInterval);
    long togo = finishTime.minus(currTime, simInterval);
    
    std::cout << "Simulation progress: " << std::endl
              << " - Start  " << startTime.toString() << std::endl
              << " - End    " << finishTime.toString() << std::endl
              << " - Now at " << currTime.toString() << std::endl
              << " - " << std::setprecision(2) << double(complete)/double(total)*100 << "% complete" << std::endl;//, approximately " 
              //<< utility::timeDisplay(togo*lastIteration) << " remaining." << std::endl;
}