/*

Copyright (c) 2005-2013, University of Oxford.
All rights reserved.

University of Oxford means the Chancellor, Masters and Scholars of the
University of Oxford, having an administrative office at Wellington
Square, Oxford OX1 2JD, UK.

This file is part of Chaste.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
 * Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name of the University of Oxford nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "UblasCustomFunctions.hpp"
#include "HeartConfig.hpp"
#include "PostProcessingWriter.hpp"
#include "PetscTools.hpp"
#include "OutputFileHandler.hpp"
#include "DistanceMapCalculator.hpp"
#include "PseudoEcgCalculator.hpp"
#include "Version.hpp"
#include "HeartEventHandler.hpp"
#include "Hdf5DataWriter.hpp"
#include "Hdf5ToMeshalyzerConverter.hpp"
#include "Hdf5ToVtkConverter.hpp"

#include <iostream>

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::PostProcessingWriter(AbstractTetrahedralMesh<ELEMENT_DIM,SPACE_DIM>& rMesh,
                                                                   const FileFinder& rDirectory,
                                                                   std::string hdf5File,
                                                                   std::string voltageName)
        : mDirectory(rDirectory),
          mHdf5File(hdf5File),
          mVoltageName(voltageName),
          mrMesh(rMesh)
{
    mLo = mrMesh.GetDistributedVectorFactory()->GetLow();
    mHi = mrMesh.GetDistributedVectorFactory()->GetHigh();
    mpDataReader = new Hdf5DataReader(mDirectory, mHdf5File);
    mpCalculator = new PropagationPropertiesCalculator(mpDataReader, voltageName);
    // Check that the hdf file was generated by simulations from (probably) the same mesh.
    assert(mpDataReader->GetNumberOfRows() == mrMesh.GetNumNodes());
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WritePostProcessingFiles()
{
    //Check that post-processing is really needed
    assert(HeartConfig::Instance()->IsPostProcessingRequested());
    //Check that it's safe to send the results to the (hard-coded) subfolder for Meshalyzer/CMGui
    assert(HeartConfig::Instance()->GetVisualizeWithMeshalyzer() || HeartConfig::Instance()->GetVisualizeWithCmgui());

    // Please note that only the master processor should write to file.
    // Each of the private methods called here takes care of checking.
    if (HeartConfig::Instance()->IsApdMapsRequested())
    {
        std::vector<std::pair<double,double> > apd_maps;
        HeartConfig::Instance()->GetApdMaps(apd_maps);
        for (unsigned i=0; i<apd_maps.size(); i++)
        {
            WriteApdMapFile(apd_maps[i].first, apd_maps[i].second);
        }
    }

    if (HeartConfig::Instance()->IsUpstrokeTimeMapsRequested())
    {
        std::vector<double> upstroke_time_maps;
        HeartConfig::Instance()->GetUpstrokeTimeMaps(upstroke_time_maps);
        for (unsigned i=0; i<upstroke_time_maps.size(); i++)
        {
            WriteUpstrokeTimeMap(upstroke_time_maps[i]);
        }
    }

    if (HeartConfig::Instance()->IsMaxUpstrokeVelocityMapRequested())
    {
        std::vector<double> upstroke_velocity_maps;
        HeartConfig::Instance()->GetMaxUpstrokeVelocityMaps(upstroke_velocity_maps);
        for (unsigned i=0; i<upstroke_velocity_maps.size(); i++)
        {
            WriteMaxUpstrokeVelocityMap(upstroke_velocity_maps[i]);
        }
    }

    if (HeartConfig::Instance()->IsConductionVelocityMapsRequested())
    {
        std::vector<unsigned> conduction_velocity_maps;
        HeartConfig::Instance()->GetConductionVelocityMaps(conduction_velocity_maps);

        //get the mesh here
        DistanceMapCalculator<ELEMENT_DIM, SPACE_DIM> dist_map_calculator(mrMesh);

        for (unsigned i=0; i<conduction_velocity_maps.size(); i++)
        {
            std::vector<double> distance_map;
            std::vector<unsigned> origin_surface;
            origin_surface.push_back(conduction_velocity_maps[i]);
            dist_map_calculator.ComputeDistanceMap(origin_surface, distance_map);
            WriteConductionVelocityMap(conduction_velocity_maps[i], distance_map);
        }
    }

    if (HeartConfig::Instance()->IsAnyNodalTimeTraceRequested())
    {
        std::vector<unsigned> requested_nodes;
        HeartConfig::Instance()->GetNodalTimeTraceRequested(requested_nodes);
        WriteVariablesOverTimeAtNodes(requested_nodes);
    }

    if (HeartConfig::Instance()->IsPseudoEcgCalculationRequested())
    {
        std::vector<ChastePoint<SPACE_DIM> > electrodes;
        HeartConfig::Instance()->GetPseudoEcgElectrodePositions(electrodes);

        ///\todo #2359 work out how to get rid of this line
        /// needed because PseudoEcgCalculator makes its own Hdf5DataReader.
        delete mpDataReader;

        for (unsigned i=0; i<electrodes.size(); i++)
        {
            PseudoEcgCalculator<ELEMENT_DIM,SPACE_DIM,1> calculator(mrMesh,
                                                                    electrodes[i],
                                                                    mDirectory,
                                                                    mHdf5File,
                                                                    mVoltageName);
            calculator.WritePseudoEcg();
        }

        ///\todo #2359 work out how to get rid of these lines
        mpDataReader = new Hdf5DataReader(mDirectory, mHdf5File);
        mpCalculator->SetHdf5DataReader(mpDataReader);
    }
}


template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::~PostProcessingWriter()
{
    delete mpDataReader;
    delete mpCalculator;
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteOutputDataToHdf5(const std::vector<std::vector<double> >& rDataPayload,
                                                                         const std::string& rDatasetName,
                                                                         const std::string& rDatasetUnit,
                                                                         const std::string& rUnlimitedVariableName,
                                                                         const std::string& rUnlimitedVariableUnit)
{
    DistributedVectorFactory* p_factory = mrMesh.GetDistributedVectorFactory();
    FileFinder test_output("", RelativeTo::ChasteTestOutput);
    Hdf5DataWriter writer(*p_factory,
                          mDirectory.GetRelativePath(test_output),  // Path relative to CHASTE_TEST_OUTPUT
                          mHdf5File,
                          false, // to wiping
                          true,  // to extending
                          rDatasetName); // dataset name

    int apd_id = writer.DefineVariable(rDatasetName,rDatasetUnit);
    writer.DefineFixedDimension(mrMesh.GetNumNodes());
    writer.DefineUnlimitedDimension(rUnlimitedVariableName, rUnlimitedVariableUnit);
    writer.EndDefineMode();

    //Determine the maximum number of paces
    unsigned local_max_paces = 0u;
    for (unsigned node_index = 0; node_index < rDataPayload.size(); ++node_index)
    {
        if (rDataPayload[node_index].size() > local_max_paces)
        {
             local_max_paces = rDataPayload[node_index].size();
        }
    }

    unsigned max_paces = 0u;
    MPI_Allreduce(&local_max_paces, &max_paces, 1, MPI_UNSIGNED, MPI_MAX, PETSC_COMM_WORLD);

    for (unsigned pace_idx = 0; pace_idx < max_paces; pace_idx++)
    {
        Vec apd_vec = p_factory->CreateVec();
        DistributedVector distributed_vector = p_factory->CreateDistributedVector(apd_vec);
        for (DistributedVector::Iterator index = distributed_vector.Begin();
             index!= distributed_vector.End();
             ++index)
        {
            unsigned node_idx = index.Local;
            // pad with zeros if no pace defined at this node
            if (pace_idx < rDataPayload[node_idx].size() )
            {
                distributed_vector[index] = rDataPayload[node_idx][pace_idx];
            }
            else
            {
                /// \todo #1660 make a test that has different numbers of APDs at different nodes.
                NEVER_REACHED;
                //distributed_vector[index] = 0.0;
            }
        }
        writer.PutVector(apd_id, apd_vec);
        PetscTools::Destroy(apd_vec);
        writer.PutUnlimitedVariable(pace_idx);
        writer.AdvanceAlongUnlimitedDimension();
    }
    writer.Close();
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteApdMapFile(double repolarisationPercentage, double threshold)
{
    std::vector<std::vector<double> > local_output_data = mpCalculator->CalculateAllActionPotentialDurationsForNodeRange(repolarisationPercentage, mLo, mHi, threshold);

    // HDF5 shouldn't have minus signs in the data names..
    std::stringstream hdf5_dataset_name;
    hdf5_dataset_name << "Apd_" << repolarisationPercentage;

    WriteOutputDataToHdf5(local_output_data,
                          hdf5_dataset_name.str() + ConvertToHdf5FriendlyString(threshold) + "_Map",
                          "msec");
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteUpstrokeTimeMap(double threshold)
{
    std::vector<std::vector<double> > output_data;
    //Fill in data
    for (unsigned node_index = mLo; node_index < mHi; node_index++)
    {
        std::vector<double> upstroke_times;
        try
        {
            upstroke_times = mpCalculator->CalculateUpstrokeTimes(node_index, threshold);
            assert(upstroke_times.size() != 0);
        }
        catch(Exception&)
        {
            upstroke_times.push_back(0);
            assert(upstroke_times.size() == 1);
        }
        output_data.push_back(upstroke_times);
    }

    WriteOutputDataToHdf5(output_data,
                          "UpstrokeTimeMap" + ConvertToHdf5FriendlyString(threshold),
                          "msec");
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteMaxUpstrokeVelocityMap(double threshold)
{
    std::vector<std::vector<double> > output_data;
    //Fill in data
    for (unsigned node_index = mLo; node_index < mHi; node_index++)
    {
        std::vector<double> upstroke_velocities;
        try
        {
            upstroke_velocities = mpCalculator->CalculateAllMaximumUpstrokeVelocities(node_index, threshold);
            assert(upstroke_velocities.size() != 0);
        }
        catch(Exception&)
        {
            upstroke_velocities.push_back(0);
            assert(upstroke_velocities.size() ==1);
        }
        output_data.push_back(upstroke_velocities);
    }

    WriteOutputDataToHdf5(output_data,
                          "MaxUpstrokeVelocityMap" + ConvertToHdf5FriendlyString(threshold),
                          "mV_per_msec");
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteConductionVelocityMap(unsigned originNode, std::vector<double> distancesFromOriginNode)
{
    //Fill in data
    std::vector<std::vector<double> > output_data;
    for (unsigned dest_node = mLo; dest_node < mHi; dest_node++)
    {
        std::vector<double> conduction_velocities;
        try
        {
            conduction_velocities = mpCalculator->CalculateAllConductionVelocities(originNode, dest_node, distancesFromOriginNode[dest_node]);
            assert(conduction_velocities.size() != 0);
        }
        catch(Exception&)
        {
            conduction_velocities.push_back(0);
            assert(conduction_velocities.size() == 1);
        }
        output_data.push_back(conduction_velocities);
    }
    std::stringstream filename_stream;
    filename_stream << "ConductionVelocityFromNode" << originNode;

    WriteOutputDataToHdf5(output_data, filename_stream.str(), "cm_per_msec");
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteAboveThresholdDepolarisationFile(double threshold )
{
    std::vector<std::vector<double> > output_data;

    //Fill in data
    for (unsigned node_index = mLo; node_index < mHi; node_index++)
    {
        std::vector<double> upstroke_velocities;
        std::vector<unsigned> above_threshold_depolarisations;
        std::vector<double> output_item;
        bool no_upstroke_occurred = false;

        try
        {
            upstroke_velocities = mpCalculator->CalculateAllMaximumUpstrokeVelocities(node_index, threshold);
            assert(upstroke_velocities.size() != 0);
        }
        catch(Exception&)
        {
            upstroke_velocities.push_back(0);
            assert(upstroke_velocities.size() == 1);
            no_upstroke_occurred = true;
        }
        // This method won't throw any exception, so there is no need to put it into the try/catch:
        above_threshold_depolarisations =  mpCalculator->CalculateAllAboveThresholdDepolarisations(node_index, threshold);

        // Count the total above threshold depolarisations
        unsigned total_number_of_above_threshold_depolarisations = 0;
        for (unsigned ead_index = 0; ead_index< above_threshold_depolarisations.size();ead_index++)
        {
            total_number_of_above_threshold_depolarisations = total_number_of_above_threshold_depolarisations + above_threshold_depolarisations[ead_index];
        }

        // For this item, push back the number of upstrokes...
        if (no_upstroke_occurred)
        {
            output_item.push_back(0);
        }
        else
        {
            output_item.push_back((double)upstroke_velocities.size());
        }
        //... and the number of above threshold depolarisations
        output_item.push_back((double) total_number_of_above_threshold_depolarisations);

        output_data.push_back(output_item);
    }

    WriteGenericFile(output_data,
                     "AboveThresholdDepolarisations" + ConvertToHdf5FriendlyString(threshold) +
                     ".dat");
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
std::string PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::ConvertToHdf5FriendlyString(double threshold)
{
    std::stringstream dataset_stream;
    std::string extra_message = "_";
    if (threshold < 0.0)
    {
        extra_message += "minus_";
        threshold = -threshold;
    }

    // if threshold is a decimal eg: because using phenomenological model
    if (threshold - floor(threshold) > 1e-8)
    {
        // give the answer to 2dp
        dataset_stream << extra_message << floor(threshold) << "pt" << floor(threshold*100)-(floor(threshold)*100);
    }
    else
    {
        dataset_stream << extra_message << threshold;
    }

    return dataset_stream.str();
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteVariablesOverTimeAtNodes(std::vector<unsigned>& rNodeIndices)
{
    std::vector<std::string> variable_names = mpDataReader->GetVariableNames();

    //we will write one file per variable in the hdf5 file
    for (unsigned name_index=0; name_index < variable_names.size(); name_index++)
    {
        std::vector<std::vector<double> > output_data;
        if (PetscTools::AmMaster())//only master process fills the data structure
        {
            //allocate memory: NXM matrix where N = number of time steps and M number of requested nodes
            output_data.resize( mpDataReader->GetUnlimitedDimensionValues().size() );
            for (unsigned j = 0; j < mpDataReader->GetUnlimitedDimensionValues().size(); j++)
            {
                output_data[j].resize(rNodeIndices.size());
            }

            for (unsigned requested_index = 0; requested_index < rNodeIndices.size(); requested_index++)
            {
                unsigned node_index = rNodeIndices[requested_index];

                //handle permutation, if any
                if ( (mrMesh.rGetNodePermutation().size() != 0) &&
                      !HeartConfig::Instance()->GetOutputUsingOriginalNodeOrdering() )
                {
                    node_index = mrMesh.rGetNodePermutation()[ rNodeIndices[requested_index] ];
                }

                //grab the data from the hdf5 file.
                std::vector<double> time_series = mpDataReader->GetVariableOverTime(variable_names[name_index], node_index);
                assert ( time_series.size() == mpDataReader->GetUnlimitedDimensionValues().size());

                //fill the output_data data structure
                for (unsigned time_step = 0; time_step < time_series.size(); time_step++)
                {
                    output_data[time_step][requested_index] = time_series[time_step];
                }
            }
        }
        std::stringstream filename_stream;
        filename_stream << "NodalTraces_" << variable_names[name_index] << ".dat";
        WriteGenericFile(output_data, filename_stream.str());
    }
}


template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteGenericFile(std::vector<std::vector<double> >& rDataPayload, const std::string& rFileName)
{
    ///\todo #1660 this whole method to be removed.
    // At present there are no cmgui-format postprocessing maps,
    // we just use meshalyzer format so that something is generated.

    if ( HeartConfig::Instance()->GetVisualizeWithMeshalyzer() )
    {
        WriteGenericFileToMeshalyzer(rDataPayload, "output", rFileName);
    }

    if ( HeartConfig::Instance()->GetVisualizeWithCmgui() )
    {
        WriteGenericFileToMeshalyzer(rDataPayload, "cmgui_output", rFileName);
    }
}

template<unsigned ELEMENT_DIM, unsigned SPACE_DIM>
void PostProcessingWriter<ELEMENT_DIM, SPACE_DIM>::WriteGenericFileToMeshalyzer(std::vector<std::vector<double> >& rDataPayload, const std::string& rFolder, const std::string& rFileName)
{
    OutputFileHandler output_file_handler(HeartConfig::Instance()->GetOutputDirectory() + "/" + rFolder, false);
    PetscTools::BeginRoundRobin();
    {
        out_stream p_file=out_stream(NULL);
        //Open file
        if (PetscTools::AmMaster())
        {
            // Open the file for the first time
            p_file = output_file_handler.OpenOutputFile(rFileName);
        }
        else
        {
            // Append data to the existing file opened by master
            p_file = output_file_handler.OpenOutputFile(rFileName, std::ios::app);
        }
        // Write data
        for (unsigned line_number=0; line_number<rDataPayload.size(); line_number++)
        {
            for (unsigned i = 0; i < rDataPayload[line_number].size(); i++)
            {
                *p_file << rDataPayload[line_number][i] << "\t";
            }
            *p_file << std::endl;
        }

        // Last processor appends comment line
        if (PetscTools::AmTopMost())
        {
            std::string comment = "# " + ChasteBuildInfo::GetProvenanceString();
            *p_file << comment;
        }
        p_file->close();
    }
    //There's a barrier included here: Process i+1 waits for process i to close the file
    PetscTools::EndRoundRobin();
}


/////////////////////////////////////////////////////////////////////
// Explicit instantiation
/////////////////////////////////////////////////////////////////////

template class PostProcessingWriter<1,1>;
template class PostProcessingWriter<1,2>;
template class PostProcessingWriter<2,2>;
template class PostProcessingWriter<1,3>;
template class PostProcessingWriter<3,3>;
