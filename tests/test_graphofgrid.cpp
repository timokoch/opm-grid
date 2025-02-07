// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  Copyright 2024 Equinor ASA.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.

  Consult the COPYING file in the top-level source directory of this
  module for the precise wording of the license and the list of
  copyright holders.
*/

#include <config.h>

#include <dune/common/version.hh>

#define BOOST_TEST_MODULE GraphRepresentationOfGrid
#define BOOST_TEST_NO_MAIN
#include <boost/test/unit_test.hpp>
#include <opm/grid/CpGrid.hpp>
#include <dune/istl/owneroverlapcopy.hh>

#include <opm/grid/GraphOfGrid.hpp>
#include <opm/grid/GraphOfGridWrappers.hpp>

#include <opm/grid/utility/OpmWellType.hpp>
#include <opm/input/eclipse/Schedule/Well/Connection.hpp>
#include <opm/input/eclipse/Schedule/Well/WellConnections.hpp>
#include <opm/input/eclipse/Schedule/Well/Well.hpp>

// basic test to check if the graph was constructed correctly
BOOST_AUTO_TEST_CASE(SimpleGraph)
{
    Dune::CpGrid grid;
    std::array<int,3> dims{2,2,2};
    std::array<double,3> size{2.,2.,2.};
    grid.createCartesian(dims,size);
    Opm::GraphOfGrid gog(grid);

    BOOST_REQUIRE(gog.size()==8); // number of graph vertices
    BOOST_REQUIRE(gog.numEdges(0)==3); // each vertex has 3 neighbors

    auto edgeL = gog.edgeList(2);
    BOOST_REQUIRE(edgeL.size()==3); // neighbors of vertex 2 are: 0, 3, 6
    BOOST_REQUIRE(edgeL[0]==1.);
    BOOST_REQUIRE(edgeL[3]==1.);
    BOOST_REQUIRE(edgeL[6]==1.);
    BOOST_REQUIRE_THROW(edgeL.at(4),std::out_of_range); // not a neighbor (edgeL's size increased)

    BOOST_REQUIRE_THROW(gog.edgeList(10),std::logic_error); // vertex 10 is not in the graph
}

// test vertex contraction on a simple graph
BOOST_AUTO_TEST_CASE(SimpleGraphWithVertexContraction)
{
    Dune::CpGrid grid;
    std::array<int,3> dims{2,2,2};
    std::array<double,3> size{2.,2.,2.};
    grid.createCartesian(dims,size);
    Opm::GraphOfGrid gog(grid);

    auto edgeL = gog.edgeList(3); // std::map<int,float>(gID,edgeWeight)
    BOOST_REQUIRE(edgeL[1]==1);
    BOOST_REQUIRE_THROW(edgeL.at(0),std::out_of_range);
    gog.contractVertices(0,1);
    BOOST_REQUIRE(gog.size()==7);
    edgeL = gog.edgeList(3);
    BOOST_REQUIRE_THROW(edgeL.at(1),std::out_of_range);
    BOOST_REQUIRE(edgeL[0]==1);
    edgeL = gog.edgeList(0);
    BOOST_REQUIRE(edgeL.size()==4);
    BOOST_REQUIRE(edgeL[2]==1); // neighbor of 0
    BOOST_REQUIRE(edgeL[3]==1); // neighbor of 1
    BOOST_REQUIRE_THROW(edgeL.at(1),std::out_of_range); // removed vertex, former neighbor of 0

    gog.contractVertices(0,2);
    BOOST_REQUIRE(gog.size()==6);
    BOOST_REQUIRE(gog.getVertex(0).weight==3.);
    edgeL = gog.edgeList(0);
    BOOST_REQUIRE(edgeL.size()==4);
    BOOST_REQUIRE(edgeL[3]==2);
    BOOST_REQUIRE(gog.edgeList(3).size()==2);
    BOOST_REQUIRE(gog.edgeList(3).at(0)==2);
    // contracting vertices removes higher ID from the graph
    // (when well is added, IDs removed from the graph are stored in the well)
    BOOST_REQUIRE_THROW(gog.getVertex(1),std::logic_error);

    auto v5e = gog.getVertex(5).edges;
    BOOST_REQUIRE(v5e==gog.edgeList(5));
    BOOST_REQUIRE(v5e==gog.edgeList(6)); // 5 and 6 have the same neighbors (1, 2 got merged)
    BOOST_REQUIRE(v5e!=gog.edgeList(7));

}

BOOST_AUTO_TEST_CASE(WrapperForZoltan)
{
    Dune::CpGrid grid;
    std::array<int,3> dims{5,4,3};
    std::array<double,3> size{1.,1.,1.};
    grid.createCartesian(dims,size);
    Opm::GraphOfGrid gog(grid);

    int err;
    int nVer = getGraphOfGridNumVertices(&gog,&err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(nVer == 60);

    std::vector<int> gIDs(nVer);
    std::vector<float> objWeights(nVer);
    getGraphOfGridVerticesList(&gog, 1, 1, gIDs.data(), nullptr, 1, objWeights.data(), &err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(objWeights[18]==1); // all weights are 1 at this point

    std::vector<int> numEdges(nVer);
    getGraphOfGridNumEdges(&gog, 1, 1, nVer, gIDs.data(), nullptr, numEdges.data(), &err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    int nEdges=0;
    for (int i=0; i<nVer; ++i)
    {
        switch (gIDs[i])
        {
            case 0:  BOOST_REQUIRE(numEdges[i]==3); break;
            case 9:  BOOST_REQUIRE(numEdges[i]==4); break;
            case 37: BOOST_REQUIRE(numEdges[i]==5); break;
            case 26: BOOST_REQUIRE(numEdges[i]==6); break;
        }
        nEdges += numEdges[i];
    }
    BOOST_REQUIRE(nEdges==266);

    std::vector<int> nborGIDs(nEdges);
    std::vector<int> nborProc(nEdges);
    std::vector<float> edgeWeights(nEdges);
    getGraphOfGridEdgeList(&gog, 1, 1, nVer, gIDs.data(), nullptr, numEdges.data(), nborGIDs.data(), nborProc.data(), 1, edgeWeights.data(), &err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE_MESSAGE(nborProc[145]==0, "Implementation detail: default process in GraphofGrid is 0");
    BOOST_REQUIRE(edgeWeights[203]==1.); // all are 1., no vertices were contracted

    numEdges[16] = 8;
    std::string message("Expecting an error message from getGraphOfGridEdgeList, the vertex "
                        + std::to_string(gIDs[16]) + std::string(" has a wrong number of edges."));
    Opm::OpmLog::info(message);
    getGraphOfGridEdgeList(&gog, 1, 1, nVer, gIDs.data(), nullptr, numEdges.data(), nborGIDs.data(), nborProc.data(), 1, edgeWeights.data(), &err);
    BOOST_REQUIRE(err==ZOLTAN_FATAL);
}

BOOST_AUTO_TEST_CASE(GraphWithWell)
{
    Dune::CpGrid grid;
    std::array<int,3> dims{5,4,3};
    std::array<double,3> size{1.,1.,1.};
    grid.createCartesian(dims,size);
    Opm::GraphOfGrid gog(grid);

    std::unordered_map<std::string, std::set<int>> wells{
        {"shape L on the front face", {5,10,15,35,55} },
        {"lying 8 on the right face", {20,1,41,22,3,43,24} },
        {"disconnected vertices", {58,12} } };
    addFutureConnectionWells(gog,wells);
    BOOST_REQUIRE(gog.getWells().size()==3);
    int err;
    int nVer = getGraphOfGridNumVertices(&gog,&err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(nVer == 49);

    std::vector<int> gIDs(nVer);
    std::vector<float> objWeights(nVer);
    getGraphOfGridVerticesList(&gog, 1, 1, gIDs.data(), nullptr, 1, objWeights.data(), &err);
    BOOST_REQUIRE(err=ZOLTAN_OK);
    for (int i=0; i<nVer; ++i)
    {
        switch (gIDs[i])
        {
            case 1:  BOOST_REQUIRE(objWeights[i]==7.); break;
            case 5:  BOOST_REQUIRE(objWeights[i]==5.); break;
            case 12: BOOST_REQUIRE(objWeights[i]==2.); break;
            default: BOOST_REQUIRE(objWeights[i]==1.); // ordinary vertex
        }
    }
}

BOOST_AUTO_TEST_CASE(IntersectingWells)
{
    Dune::CpGrid grid;
    std::array<int,3> dims{5,4,3};
    std::array<double,3> size{1.,1.,1.};
    grid.createCartesian(dims,size);
    Opm::GraphOfGrid gog(grid);

    std::array<std::set<int>,3> wells{std::set<int>{0,1,2,3,4},
                                      std::set<int>{52,32,12},
                                      std::set<int>{59,48,37}};
                        // later add  std::set<int>{37,38,39,34},
                        //                         {2,8} and {2,38}
    for (const auto& w : wells)
    {
        gog.addWell(w,false);
    }
    BOOST_REQUIRE(gog.getWells().size()==3);

    int err;
    int nVer = getGraphOfGridNumVertices(&gog,&err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(nVer == 52);

    gog.addWell(std::set<int>{37,38,39,34}); // intersects with previous
    BOOST_REQUIRE(gog.getWells().size()==3);
    nVer = getGraphOfGridNumVertices(&gog,&err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(nVer == 49);

    gog.addWell(std::set<int>{2,8});
    nVer = getGraphOfGridNumVertices(&gog,&err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(nVer == 48);

    gog.addWell(std::set<int>{2,38}); // joins two wells
    BOOST_REQUIRE(gog.getWells().size()==2);
    nVer = getGraphOfGridNumVertices(&gog,&err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(nVer == 47);

    gog.addWell(std::set<int>{8,38});
    nVer = getGraphOfGridNumVertices(&gog,&err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(nVer == 47);

    std::vector<int> gIDs(nVer);
    std::vector<float> objWeights(nVer);
    getGraphOfGridVerticesList(&gog, 1, 1, gIDs.data(), nullptr, 1, objWeights.data(), &err);
    BOOST_REQUIRE(err=ZOLTAN_OK);

    for (int i=0; i<nVer; ++i)
    {
        switch (gIDs[i])
        {
            case 0:  BOOST_REQUIRE(objWeights[i]==12.); break;
            case 12: BOOST_REQUIRE(objWeights[i]==3.); break;
            default: BOOST_REQUIRE(objWeights[i]==1.); // ordinary vertex
        }
    }

    int nOut = 3;
    std::vector<int> numEdges(nOut);
    std::vector<int> gID{12,0,54};
    getGraphOfGridNumEdges(&gog, 1, 1, nOut, gID.data(), nullptr, numEdges.data(), &err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(numEdges[0]==12);
    BOOST_REQUIRE(numEdges[1]==26);
    BOOST_REQUIRE(numEdges[2]==3);

    int nEdges = 41;
    std::vector<int> nborGIDs(nEdges);
    std::vector<int> nborProc(nEdges);
    std::vector<float> edgeWeights(nEdges);
    getGraphOfGridEdgeList(&gog, 1, 1, nOut, gID.data(), nullptr, numEdges.data(), nborGIDs.data(), nborProc.data(), 1, edgeWeights.data(), &err);
    BOOST_REQUIRE(err==ZOLTAN_OK);

    // neighbors of the well with cells 12, 32, 52
    int checked = 0;
    for (int i=0; i<12; ++i)
    {
        BOOST_REQUIRE(edgeWeights[i]==1);
        switch (nborGIDs[i])
        {
            case  7: case 11: case 13: case 17:
            case 27: case 31: case 33: case  0: // 37 is a well with ID 0
            case 47: case 51: case 53: case 57:
                ++checked;
        }
    }
    BOOST_REQUIRE(checked==12);

    // neighbors of the well with cells 0,1,2,3,4,8,34,37,38,39,48,59
    checked=0;
    for (int i=12; i<38; ++i)
    {
        switch (nborGIDs[i])
        {
            // neighboring two well cells adds up the edge weight
            case 7: case 9: case 28: case 33: case 54: case 58:
                BOOST_REQUIRE(edgeWeights[i]==2.);
                ++checked;
                break;
            default: BOOST_REQUIRE(edgeWeights[i]==1.);
        }
    }
    BOOST_REQUIRE(checked==6);
    checked=0;

    // neighbors of the cell with global ID 54
    for (int i=38; i<41; ++i)
    {
        switch (nborGIDs[i])
        {
            case 0: // contains cells 34 and 59
                ++checked;
                BOOST_REQUIRE(edgeWeights[i]==2.);
                break;
            case 49: case 53:
                ++checked;
                BOOST_REQUIRE(edgeWeights[i]==1.);
        }
    }
    BOOST_REQUIRE(checked==3);

    const auto& wellList = gog.getWells();
    BOOST_REQUIRE(wellList.size()==2);
    std::set<int> well1{12,32,52};
    std::set<int> well2{0,1,2,3,4,8,34,37,38,39,48,59};
    if (wellList.begin()->size()==3)
    {
        BOOST_REQUIRE( *wellList.begin()==well1 );
        BOOST_REQUIRE( *wellList.rbegin()==well2 );
    }
    else
    {
        BOOST_REQUIRE( *wellList.begin()==well2 );
        BOOST_REQUIRE( *wellList.rbegin()==well1 );
    }
}

// Create yet another small grid with wells and test graph properties.
// This time wells are supplied via OpmWellType interface
BOOST_AUTO_TEST_CASE(addWellConnections)
{
    // create a grid
    Dune::CpGrid grid;
    std::array<int,3> dims{2,2,2};
    std::array<double,3> size{1.,1.,1.};
    grid.createCartesian(dims,size);
    Opm::GraphOfGrid gog(grid);
    BOOST_REQUIRE(gog.size()==8);

    // create Wells, we only use well name and cell locations
    auto createConnection = [](int i, int j, int k)
    {
        return Opm::Connection(i,j,k,0, 0,Opm::Connection::State::OPEN,
                                   Opm::Connection::Direction::Z,
                                   Opm::Connection::CTFKind::DeckValue, 0,
                                   5.,Opm::Connection::CTFProperties(),0,false);
    };
    auto createWell = [](const std::string& name)
    {
        using namespace Opm;
        return Dune::cpgrid::OpmWellType(name,name,0,0,0,0,0.,WellType(),
                   Well::ProducerCMode(),Connection::Order(),UnitSystem(),
                   0.,0.,false,false,0,Well::GasInflowEquation());
    };

    auto wellCon = std::make_shared<Opm::WellConnections>(); // do not confuse with Dune::cpgrid::WellConnections
    wellCon->add(createConnection(0,0,0));
    wellCon->add(createConnection(0,1,0));
    wellCon->add(createConnection(0,1,1));
    std::vector<Dune::cpgrid::OpmWellType> wells;
    wells.push_back(createWell("first"));
    wells[0].updateConnections(wellCon,true);

    wellCon = std::make_shared<Opm::WellConnections>(); //reset
    wellCon->add(createConnection(0,0,1));
    wellCon->add(createConnection(1,1,0));
    wells.push_back(createWell("second"));
    wells[1].updateConnections(wellCon,true);

    wellCon = std::make_shared<Opm::WellConnections>(); //reset
    wellCon->add(createConnection(0,0,1));
    wellCon->add(createConnection(1,0,1));
    wells.push_back(createWell("third")); // intersects with second
    wells[2].updateConnections(wellCon,true);

    Dune::cpgrid::WellConnections wellConnections(wells,std::unordered_map<std::string, std::set<int>>(),gog.getGrid());
    BOOST_REQUIRE(wellConnections.size()==3);
    BOOST_REQUIRE(wellConnections[0]==(std::set<int>{0,2,6}));
    BOOST_REQUIRE(wellConnections[1].size()==2);
    BOOST_REQUIRE(wellConnections[1]==(std::set<int>{3,4}));
    BOOST_REQUIRE(wellConnections[2].size()==2);
    BOOST_REQUIRE(wellConnections[2]==(std::set<int>{4,5}));

    Opm::addWellConnections(gog,wellConnections,true);
    BOOST_REQUIRE(gog.size()==4);
    BOOST_REQUIRE(gog.getWells().size()==2); // second and third got merged (in gog)

    int err;
    int nVer = getGraphOfGridNumVertices(&gog,&err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(nVer == 4);
    std::vector<int> gIDs(nVer);
    std::vector<float> objWeights(nVer);
    getGraphOfGridVerticesList(&gog, 1, 1, gIDs.data(), nullptr, 1, objWeights.data(), &err);
    BOOST_REQUIRE(err=ZOLTAN_OK);
    std::sort(gIDs.begin(),gIDs.end());
    BOOST_REQUIRE(gIDs[0]==0 && gIDs[1]==1 && gIDs[2]==3 && gIDs[3]==7);
    std::vector<int> numEdges(nVer);
    getGraphOfGridNumEdges(&gog, 1, 1, nVer, gIDs.data(), nullptr, numEdges.data(), &err);
    BOOST_REQUIRE(err==ZOLTAN_OK);
    BOOST_REQUIRE(numEdges[0]==3 && numEdges[1]==2 && numEdges[2]==3 && numEdges[3]==2);
    int nEdges = 10; // sum of numEdges[i]
    std::vector<int> nborGIDs(nEdges), nborProc(nEdges);
    std::vector<float> edgeWeights(nEdges);
    getGraphOfGridEdgeList(&gog, 1, 1, nVer, gIDs.data(), nullptr, numEdges.data(), nborGIDs.data(), nborProc.data(), 1, edgeWeights.data(), &err);
    BOOST_REQUIRE(err==ZOLTAN_OK);

    // check all edgeWeights. Note that nborGIDs are not sorted
    for (int i=0; i<3; ++i)
    {
        switch (nborGIDs[i])
        {
            case 1: BOOST_REQUIRE(edgeWeights[i]==1); break;
            case 3: BOOST_REQUIRE(edgeWeights[i]==3); break;
            case 7: BOOST_REQUIRE(edgeWeights[i]==1); break;
            default: throw("GraphOfGrid was constructed badly.");
        }
    }
    for (int i=3; i<5; ++i)
    {
        switch (nborGIDs[i])
        {
            case 0: BOOST_REQUIRE(edgeWeights[i]==1); break;
            case 3: BOOST_REQUIRE(edgeWeights[i]==2); break;
            default: throw("GraphOfGrid was constructed badly.");
        }
    }
    for (int i=5; i<8; ++i)
    {
        switch (nborGIDs[i])
        {
            case 0: BOOST_REQUIRE(edgeWeights[i]==3); break;
            case 1: BOOST_REQUIRE(edgeWeights[i]==2); break;
            case 7: BOOST_REQUIRE(edgeWeights[i]==2); break;
            default: throw("GraphOfGrid was constructed badly.");
        }
    }
    for (int i=8; i<10; ++i)
    {
        switch (nborGIDs[i])
        {
            case 0: BOOST_REQUIRE(edgeWeights[i]==1); break;
            case 3: BOOST_REQUIRE(edgeWeights[i]==2); break;
            default: throw("GraphOfGrid was constructed badly.");
        }
    }

}

// After partitioning, importList and exportList are not complete,
// other cells from wells need to be added.
BOOST_AUTO_TEST_CASE(ImportExportListExpansion)
{
    // create a grid with wells
    Dune::CpGrid grid;
    std::array<int,3> dims{2,3,2};
    std::array<double,3> size{1.,1.,1.};
    grid.createCartesian(dims,size);
    Opm::GraphOfGrid gog(grid);
    gog.addWell(std::set<int>{0,1,2});
    gog.addWell(std::set<int>{5,8,11});
    const auto& wells = gog.getWells();
    BOOST_REQUIRE(wells.size()==2);

    // mock import and export lists
    using importTuple = std::tuple<int,int,char,int>;
    using exportTuple = std::tuple<int,int,char>;
    using AttributeSet = Dune::cpgrid::CpGridData::AttributeSet;

    std::vector<importTuple> imp(3);
    imp[0] = std::make_tuple(0,1,AttributeSet::owner,1);
    imp[1] = std::make_tuple(3,4,AttributeSet::copy,2);
    imp[2] = std::make_tuple(5,0,AttributeSet::copy,3);
    extendImportExportList(gog,imp);
    BOOST_REQUIRE(imp.size()==7);
    BOOST_CHECK(std::get<0>(imp[5])==8);
    BOOST_CHECK(std::get<1>(imp[5])==0);
    BOOST_CHECK(std::get<2>(imp[5])==AttributeSet::copy);
    BOOST_CHECK(std::get<3>(imp[5])==3);
    BOOST_CHECK(std::get<0>(imp[5])==8);
    BOOST_CHECK(std::get<1>(imp[1])==1);
    BOOST_CHECK(std::get<2>(imp[1])==AttributeSet::owner);
    BOOST_CHECK(std::get<3>(imp[1])==1);

    std::vector<exportTuple> exp(3);
    exp[0] = std::make_tuple(0,1,AttributeSet::owner);
    exp[1] = std::make_tuple(3,4,AttributeSet::copy);
    exp[2] = std::make_tuple(5,0,AttributeSet::copy);
    extendImportExportList(gog,exp);
    BOOST_CHECK(std::get<0>(imp[5])==8);
    BOOST_CHECK(std::get<1>(imp[5])==0);
    BOOST_CHECK(std::get<2>(imp[5])==AttributeSet::copy);
    BOOST_CHECK(std::get<0>(imp[5])==8);
    BOOST_CHECK(std::get<1>(imp[1])==1);
    BOOST_CHECK(std::get<2>(imp[1])==AttributeSet::owner);
}

bool
init_unit_test_func()
{
    return true;
}

int main(int argc, char** argv)
{
    Dune::MPIHelper::instance(argc, argv);
    boost::unit_test::unit_test_main(&init_unit_test_func,
                                     argc, argv);
}
