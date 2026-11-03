#pragma once
#include "engine/simulator/layout.hpp"
#include "engine/simulator/occupancy.hpp"
#include <source_location>
#include <climits>
inline void layoutChecks() {
    using namespace engine::simulator;
    auto check=[](bool ok,std::source_location at=std::source_location::current()){if(!ok)throw std::runtime_error("Layout line "+std::to_string(at.line()));};
    check(GridLayout::snap(-3.76,-6,.5)==4);check(!GridLayout::snap(INFINITY,0,1));check(!GridLayout::snap(0,0,0));
    GridLayout board{12,12,{},{{0,0},{11,11}}};
    check(board.place({1,3,3,2,4,0})==LayoutError::None);auto saved=board.placements();
    check(GridLayout::footprint(saved[0]).depth==4 && GridLayout::access(saved[0])==GridCell{4,7});
    check(board.place({1,0,0,1,1,0})==LayoutError::Duplicate && board.placements()==saved);
    check(board.move(1,-1,3,0)==LayoutError::Bounds && board.placements()==saved);
    check(board.move(1,3,3,4)==LayoutError::Invalid && board.placements()==saved);
    check(board.place({2,4,4,2,2,0})==LayoutError::Overlap && board.placements()==saved);
    check(board.place({2,5,3,2,2,0})==LayoutError::Clearance && board.placements()==saved);
    check(board.move(1,3,3,1)==LayoutError::None && GridLayout::access(board.placements()[0])==GridCell{2,4});
    check(board.move(1,3,3,2)==LayoutError::None && GridLayout::access(board.placements()[0])==GridCell{3,2});
    check(board.move(1,3,3,3)==LayoutError::None && GridLayout::access(board.placements()[0])==GridCell{7,3});
    check(board.restore(saved)==LayoutError::None);check(board.blocked({3,3}) && !board.blocked({4,7}));
    GridLayout corridor{8,8,{{3,0,1,3},{3,5,1,3}},{{1,1},{6,6}}};
    check(corridor.place({1,3,3,2,2,3})==LayoutError::Unreachable && corridor.placements().empty());
    auto invalid=saved;invalid[0].width=INT_MAX;check(board.restore(invalid)==LayoutError::Invalid && board.placements()==saved);
    check(board.erase(1)==LayoutError::None && board.erase(1)==LayoutError::Missing);
    OccupancyPool<> seats{2,4};CustomerId first{1,1},second{2,1},third{3,1};
    check(bool(seats.setEnabled(std::array<std::uint8_t,2>{0,1})));check(bool(seats.request(first)) && seats.claim(first)->point==1);
    auto claim=seats.claim(first).value();check(!seats.setEnabled(std::array<std::uint8_t,2>{0,0}) && seats.claim(first)->token==claim.token);
    check(bool(seats.request(second)) && seats.claim(second)->point==OccupancyPool<>::waiting);
    check(bool(seats.setEnabled(std::array<std::uint8_t,2>{1,1})) && seats.claim(second)->point==0);
    check(!seats.setEnabled(std::array<std::uint8_t,2>{2,1}));
    check(bool(seats.release(first)) && bool(seats.release(second)));
    check(bool(seats.setEnabled(std::array<std::uint8_t,2>{0,0})) && bool(seats.request(third)) && seats.claim(third)->point==OccupancyPool<>::waiting);
    auto records=seats.claims();check(bool(seats.restore(records)));
    records[0].point=0;check(!seats.restore(records) && seats.claim(third)->point==OccupancyPool<>::waiting);
}
