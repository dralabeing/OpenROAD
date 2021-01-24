/////////////////////////////////////////////////////////////////////////////
// Original authors: SangGi Do(sanggido@unist.ac.kr), Mingyu
// Woo(mwoo@eng.ucsd.edu)
//          (respective Ph.D. advisors: Seokhyeong Kang, Andrew B. Kahng)
// Rewrite by James Cherry, Parallax Software, Inc.
//
// Copyright (c) 2019, OpenROAD
// Copyright (c) 2018, SangGi Do and Mingyu Woo
// All rights reserved.
//
// BSD 3-Clause License
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
///////////////////////////////////////////////////////////////////////////////

#include "opendp/Opendp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include "utility/Logger.h"
#include "openroad/OpenRoad.hh"

//#define ODP_DEBUG

namespace dpl {

using std::abs;
using std::max;
using std::min;
using std::numeric_limits;
using std::sort;
using std::string;
using std::vector;
using std::cout;
using std::endl;

using ord::closestPtInRect;
using utl::DPL;

static bool
cellAreaGreater(const Cell *cell1, const Cell *cell2);

void
Opendp::detailedPlacement()
{
  initGrid();
  if (!groups_.empty()) {
    placeGroups();
  }
  place();
}

Point
Opendp::prePlaceLocation(const Cell *cell,
                         bool padded) const
{
  if (isFixed(cell))
    logger_->critical(DPL, 26, "prePlaceLocation called on fixed cell.");

  Point init = initialLocation(cell, padded);
  Point legal = legalLocation(cell, init);
  return legal;
}

// Legalize pt origin for cell
//  inside the core
//  not on top of a macro
//  row site
Point
Opendp::legalLocation(const Cell *cell,
                      Point pt) const
{
  // Move inside core.
  int core_x = min(max(0, pt.getX()), row_site_count_ * site_width_ - cell->width_);
  int core_y = min(max(0, pt.getY()), row_count_ * row_height_ - cell->height_);

  // Align with row site.
  int grid_x = divRound(core_x, site_width_);
  int grid_y = divRound(core_y, row_height_);
  int legal_x = grid_x * site_width_;
  int legal_y = grid_y * row_height_;

  // Move std cells off of macros.
  Pixel *pixel = gridPixel(grid_x, grid_y);
  if (pixel) {
    const Cell *block = pixel->cell;
    if (block
        && isBlock(block)) {
      Rect block_bbox(block->x_, block->y_,
                      block->x_ + block->width_, block->y_ + block->height_);
      if (legal_x >= block_bbox.xMin()
          && legal_x <= block_bbox.xMax()
          && legal_y >= block_bbox.yMin()
          && legal_y <= block_bbox.yMax()) {
        int x_dist_min = legal_x - block_bbox.xMin();
        int x_dist_max = block_bbox.xMax() - legal_x;
        int y_dist_min = legal_y - block_bbox.yMin();
        int y_dist_max = block_bbox.yMax() - legal_y;
        if (x_dist_min < x_dist_max
            && x_dist_min < y_dist_min
            && x_dist_min < y_dist_max) {
          // left block
          int off_x = block_bbox.xMin() - cell->width_;
          // re-legalize
          core_x = min(max(0, off_x), row_site_count_ * site_width_ - cell->width_);
          legal_x = divRound(core_x, site_width_) * site_width_;
        }
        else if (x_dist_max <= x_dist_min
                 && x_dist_max <= y_dist_min
                 && x_dist_max <= y_dist_max) {
          // right block
          int off_x = divCeil(block_bbox.xMax(), site_width_) * site_width_;
          // re-legalize
          core_x = min(max(0, off_x), row_site_count_ * site_width_ - cell->width_);
          grid_x = divRound(core_x, site_width_) * site_width_;
        }
        else if (y_dist_min <= x_dist_min
                 && y_dist_min <= x_dist_max
                 && y_dist_min <= y_dist_max) {
          // below block
          int off_y = divFloor(block_bbox.yMin(),row_height_)*row_height_-cell->height_;
          // re-legalize
          core_y = min(max(0, off_y), row_count_ * row_height_ - cell->height_);
          legal_y = divRound(core_y, row_height_) * row_height_;
        }
        else if (y_dist_max <= x_dist_min
                 && y_dist_max <= x_dist_max
                 && y_dist_max <= y_dist_min) {
          // above block
          int off_y = divCeil(block_bbox.yMax(), row_height_) * row_height_;
          // re-legalize
          core_y = min(max(0, off_y), row_count_ * row_height_ - cell->height_);
          legal_y = divRound(core_y, row_height_) * row_height_;
        }
      }
    }
  }
  return Point(legal_x, legal_y);
}

void
Opendp::placeGroups()
{
  group_cell_region_assign();

  prePlaceGroups();
  prePlace();

  // naive placement method ( multi -> single )
  placeGroups2();
  for (Group &group : groups_) {
    // magic number alert
    for (int pass = 0; pass < 3; pass++) {
      int refine_count = groupRefine(&group);
      int anneal_count = anneal(&group);
      // magic number alert
      if (refine_count < 10 || anneal_count < 100) {
        break;
      }
    }
  }
}

void
Opendp::prePlace()
{
  for (Cell &cell : cells_) {
    Rect *target = nullptr;
    if (!cell.inGroup() && !cell.is_placed_) {
      for (Group &group : groups_) {
        for (Rect &rect : group.regions) {
          if (checkOverlap(&cell, &rect)) {
            target = &rect;
          }
        }
      }
      if (target) {
        Point nearest = nearestPt(&cell, target);
        if (mapMove(&cell, nearest.x(), nearest.y())) {
          cell.hold_ = true;
        }
      }
    }
  }
}

bool
Opendp::checkOverlap(const Cell *cell, const Rect *rect) const
{
  Point init = initialLocation(cell, true);
  int x = init.getX();
  int y = init.getY();
  return x + paddedWidth(cell) > rect->xMin()
    && x < rect->xMax()
    && y + cell->height_ > rect->yMin()
    && y < rect->yMax();
}

Point
Opendp::nearestPt(const Cell *cell, const Rect *rect) const
{
  Point init = prePlaceLocation(cell, false);
  int x = init.getX();
  int y = init.getY();
  
  int temp_x = x;
  int temp_y = y;

  if (checkOverlap(cell, rect)) {
    int dist_x, dist_y;
    if (abs(x - rect->xMin() + paddedWidth(cell)) > abs(rect->xMax() - x)) {
      dist_x = abs(rect->xMax() - x);
      temp_x = rect->xMax();
    }
    else {
      dist_x = abs(x - rect->xMin());
      temp_x = rect->xMin() - paddedWidth(cell);
    }
    if (abs(y - rect->yMin() + cell->height_) > abs(rect->yMax() - y)) {
      dist_y = abs(rect->yMax() - y);
      temp_y = rect->yMax();
    }
    else {
      dist_y = abs(y - rect->yMin());
      temp_y = rect->yMin() - cell->height_;
    }
    if (dist_x < dist_y) {
      return Point(temp_x, y);
    }
    return Point(x, temp_y);
  }

  if (x < rect->xMin()) {
    temp_x = rect->xMin();
  }
  else if (x + paddedWidth(cell) > rect->xMax()) {
    temp_x = rect->xMax() - paddedWidth(cell);
  }

  if (y < rect->yMin()) {
    temp_y = rect->yMin();
  }
  else if (y + cell->height_ > rect->yMax()) {
    temp_y = rect->yMax() - cell->height_;
  }

  return Point(temp_x, temp_y);
}

void
Opendp::prePlaceGroups()
{
  for (Group &group : groups_) {
    for (Cell *cell : group.cells_) {
      if (!(isFixed(cell) || cell->is_placed_)) {
        int dist = numeric_limits<int>::max();
        bool in_group = false;
        Rect *nearest_rect = nullptr;
        for (Rect &rect : group.regions) {
          if (isInside(cell, &rect)) {
            in_group = true;
          }
          int rect_dist = distToRect(cell, &rect);
          if (rect_dist < dist) {
            dist = rect_dist;
            nearest_rect = &rect;
          }
        }
        if (!in_group) {
          Point nearest = nearestPt(cell, nearest_rect);
          if (mapMove(cell, nearest.x(), nearest.y())) {
            cell->hold_ = true;
          }
        }
      }
    }
  }
}

bool
Opendp::isInside(const Cell *cell, const Rect *rect) const
{
  Point init = initialLocation(cell, true);
  int x = init.getX();
  int y = init.getY();
  return x >= rect->xMin()
    && x + cell->width_ <= rect->xMax()
    && y >= rect->yMin()
    && y + cell->height_ <= rect->yMax();
}

int
Opendp::distToRect(const Cell *cell, const Rect *rect) const
{
  Point init = initialLocation(cell, true);
  int x = init.getX();
  int y = init.getY();

  int dist_x, dist_y;
  if (x < rect->xMin()) {
    dist_x = rect->xMin() - x;
  }
  else if (x + cell->width_ > rect->xMax()) {
    dist_x = x + cell->width_ - rect->xMax();
  }

  if (y < rect->yMin()) {
    dist_y = rect->yMin() - y;
  }
  else if (y + cell->height_ > rect->yMax()) {
    dist_y = y + cell->height_ - rect->yMax();
  }

  assert(dist_y >= 0);
  assert(dist_x >= 0);

  return dist_y + dist_x;
}

void
Opendp::place()
{
  vector<Cell *> sorted_cells;
  sorted_cells.reserve(cells_.size());

  for (Cell &cell : cells_) {
    if (!(isFixed(&cell) || cell.inGroup() || cell.is_placed_)) {
      sorted_cells.push_back(&cell);
      if (!cellFitsInCore(&cell)) {
        logger_->warn(DPL, 15, "instance {} does not fit inside the ROW core area.",
                      cell.name());
      }
    }
  }
  sort(sorted_cells.begin(), sorted_cells.end(), cellAreaGreater);

  // Place multi-row instances first.
  if (have_multi_height_cells_) {
    for (Cell *cell : sorted_cells) {
      if (isMultiRow(cell) && cellFitsInCore(cell)) {
        if (!mapMove(cell)) {
          shiftMove(cell);
        }
      }
    }
  }
  for (Cell *cell : sorted_cells) {
    if (!isMultiRow(cell)
        && cellFitsInCore(cell)) {
      if (!mapMove(cell)) {
        shiftMove(cell);
      }
    }
  }
  // This has negligible benefit -cherry
  // anneal();
}

bool
Opendp::cellFitsInCore(Cell *cell)
{
  return gridPaddedWidth(cell) <= row_site_count_ && gridHeight(cell) <= row_count_;
}

static bool
cellAreaGreater(const Cell *cell1, const Cell *cell2)
{
  int area1 = cell1->area();
  int area2 = cell2->area();
  return (area1 > area2)
    || (area1 == area2
        && cell1->db_inst_->getId() < cell2->db_inst_->getId());
}

void
Opendp::placeGroups2()
{
  for (Group &group : groups_) {
    vector<Cell *> group_cells;
    group_cells.reserve(cells_.size());
    for (Cell *cell : group.cells_) {
      if (!isFixed(cell) && !cell->is_placed_) {
        group_cells.push_back(cell);
      }
    }
    sort(group_cells.begin(), group_cells.end(), cellAreaGreater);

    // Place multi-row cells in each group region.
    bool multi_pass = true;
    for (Cell *cell : group_cells) {
      if (!isFixed(cell) && !cell->is_placed_) {
        assert(cell->inGroup());
        if (isMultiRow(cell)) {
          multi_pass = mapMove(cell);
          if (!multi_pass) {
            break;
          }
        }
      }
    }
    bool single_pass = true;
    if (multi_pass) {
      // Place single-row cells in each group region.
      for (Cell *cell : group_cells) {
        if (!isFixed(cell) && !cell->is_placed_) {
          assert(cell->inGroup());
          if (!isMultiRow(cell)) {
            single_pass = mapMove(cell);
            if (!single_pass) {
              break;
            }
          }
        }
      }
    }

    if (!single_pass || !multi_pass) {
      // Erase group cells
      for (Cell *cell : group.cells_) {
        erasePixel(cell);
      }

      // Determine brick placement by utilization.
      // magic number alert
      if (group.util > 0.95) {
        brickPlace1(&group);
      }
      else {
        brickPlace2(&group);
      }
    }
  }
}

// Place cells in group toward edges.
void
Opendp::brickPlace1(const Group *group)
{
  const Rect *boundary = &group->boundary;
  vector<Cell *> sorted_cells(group->cells_);

  sort(sorted_cells.begin(), sorted_cells.end(),
       [&](Cell *cell1, Cell *cell2) {
         return rectDist(cell1, boundary) < rectDist(cell2, boundary);
       });

  for (Cell *cell : sorted_cells) {
    int x, y;
    rectDist(cell, boundary, &x, &y);
    // This looks for a site starting at the nearest corner in rect,
    // which seems broken. It should start looking at the nearest point
    // on the rect boundary. -cherry
    if (!mapMove(cell, x, y)) {
      logger_->warn(DPL, 16, "cannot place instance {}.", cell->name());
    }
  }
}

void
Opendp::rectDist(const Cell *cell,
                 const Rect *rect,
                 // Return values.
                 int *x,
                 int *y) const
{
  Point init = prePlaceLocation(cell, false);
  int init_x = init.getX();
  int init_y = init.getY();

  if (init_x > (rect->xMin() + rect->xMax()) / 2) {
    *x = rect->xMax();
  }
  else {
    *x = rect->xMin();
  }

  if (init_y > (rect->yMin() + rect->yMax()) / 2) {
    *y = rect->yMax();
  }
  else {
    *y = rect->yMin();
  }
}

int
Opendp::rectDist(const Cell *cell, const Rect *rect) const
{
  int x, y;
  rectDist(cell, rect, &x, &y);
  Point init = prePlaceLocation(cell, false);
  return abs(init.getX() - x) + abs(init.getY() - y);
}

// Place group cells toward region edges.
void
Opendp::brickPlace2(const Group *group)
{
  vector<Cell *> sorted_cells(group->cells_);

  sort(sorted_cells.begin(), sorted_cells.end(),
       [&](Cell *cell1, Cell *cell2) {
         return rectDist(cell1, cell1->region_) < rectDist(cell2, cell2->region_);
       });

  for (Cell *cell : sorted_cells) {
    if (!cell->hold_) {
      int x, y;
      rectDist(cell, cell->region_, &x, &y);
      // This looks for a site starting at the nearest corner in rect,
      // which seems broken. It should start looking at the nearest point
      // on the rect boundary. -cherry
      if (!mapMove(cell, x, y))
        logger_->warn(DPL, 17, "cannot place instance {}.", cell->name());
    }
  }
}

int
Opendp::groupRefine(const Group *group)
{
  vector<Cell *> sort_by_disp(group->cells_);

  sort(sort_by_disp.begin(), sort_by_disp.end(), [&](Cell *cell1, Cell *cell2) {
    return (disp(cell1) > disp(cell2));
  });

  int count = 0;
  for (int i = 0; i < sort_by_disp.size() * group_refine_percent_; i++) {
    Cell *cell = sort_by_disp[i];
    if (!cell->hold_) {
      if (refineMove(cell)) {
        count++;
      }
    }
  }
  return count;
}

// This is NOT annealing. It is random swapping. -cherry
int
Opendp::anneal(Group *group)
{
  srand(rand_seed_);
  int count = 0;

  // magic number alert
  for (int i = 0; i < 100 * group->cells_.size(); i++) {
    Cell *cell1 = group->cells_[rand() % group->cells_.size()];
    Cell *cell2 = group->cells_[rand() % group->cells_.size()];
    if (swapCells(cell1, cell2)) {
      count++;
    }
  }
  return count;
}

// This is NOT annealing. It is random swapping. -cherry
int
Opendp::anneal()
{
  srand(rand_seed_);
  int count = 0;
  // magic number alert
  for (int i = 0; i < 100 * cells_.size(); i++) {
    Cell *cell1 = &cells_[rand() % cells_.size()];
    Cell *cell2 = &cells_[rand() % cells_.size()];
    if (swapCells(cell1, cell2)) {
      count++;
    }
  }
  return count;
}

// Not called -cherry.
int
Opendp::refine()
{
  vector<Cell *> sorted;
  sorted.reserve(cells_.size());

  for (Cell &cell : cells_) {
    if (!(isFixed(&cell) || cell.hold_ || cell.inGroup())) {
      sorted.push_back(&cell);
    }
  }
  sort(sorted.begin(), sorted.end(), [&](Cell *cell1, Cell *cell2) {
    return disp(cell1) > disp(cell2);
  });

  int count = 0;
  for (int i = 0; i < sorted.size() * refine_percent_; i++) {
    Cell *cell = sorted[i];
    if (!cell->hold_) {
      if (refineMove(cell)) {
        count++;
      }
    }
  }
  return count;
}

////////////////////////////////////////////////////////////////

bool
Opendp::mapMove(Cell *cell)
{
  Point init = prePlaceLocation(cell, true);
  return mapMove(cell, init.getX(), init.getY());
}

bool
Opendp::mapMove(Cell *cell, int x, int y)
{
  PixelPt pixel_pt = diamondSearch(cell, x, y);
  if (pixel_pt.pixel) {
    PixelPt near_pt = diamondSearch(cell,
                                    pixel_pt.pt.getX() * site_width_,
                                    pixel_pt.pt.getY() * row_height_);
    if (near_pt.pixel) {
      paintPixel(cell, near_pt.pt.getX(), near_pt.pt.getY());
    }
    else {
      paintPixel(cell, pixel_pt.pt.getX(), pixel_pt.pt.getY());
    }
    return true;
  }
  return false;
}

bool
Opendp::shiftMove(Cell *cell)
{
  Point init = prePlaceLocation(cell, true);
  int grid_x = gridX(init.getX());
  int grid_y = gridY(init.getY());
  // magic number alert
  int boundary_margin = 3;
  int margin_width = paddedWidth(cell) * boundary_margin;
  set<Cell *> region_cells;
  for (int x = grid_x - margin_width; x < grid_x + margin_width; x++) {
    for (int y = grid_y - boundary_margin; y < grid_y + boundary_margin; y++) {
      Pixel *pixel = gridPixel(x, y);
      if (pixel) {
        Cell *cell = pixel->cell;
        if (cell && !isFixed(cell))
          region_cells.insert(cell);
      }
    }
  }

  // erase region cells
  for (Cell *around_cell : region_cells) {
    if (cell->inGroup() == around_cell->inGroup()) {
      erasePixel(around_cell);
    }
  }

  // place target cell
  if (!mapMove(cell, init.getX(), init.getY())) {
    logger_->warn(DPL, 18, "detailed placement failed on {}.",
                  cell->name());
    return false;
  }

  // re-place erased cells
  for (Cell *around_cell : region_cells) {
    if (cell->inGroup() == around_cell->inGroup()) {
      if (!mapMove(around_cell)) {
        logger_->warn(DPL, 19, "detailed placement failed on {}",
                      around_cell->name());
        return false;
      }
    }
  }
  return true;
}

bool
Opendp::swapCells(Cell *cell1, Cell *cell2)
{
  if (cell1 != cell2
      && !cell1->hold_
      && !cell2->hold_
      && cell1->width_ == cell2->width_
      && cell1->height_ == cell2->height_
      && !isFixed(cell1)
      && !isFixed(cell2)) {
    int dist_change = distChange(cell1, cell2->x_, cell2->y_)
      + distChange(cell2, cell1->x_, cell1->y_);

    if (dist_change < 0) {
      int grid_x1 = gridPaddedX(cell2);
      int grid_y1 = gridY(cell2);
      int grid_x2 = gridPaddedX(cell1);
      int grid_y2 = gridY(cell1);

      erasePixel(cell1);
      erasePixel(cell2);
      paintPixel(cell1, grid_x1, grid_y1);
      paintPixel(cell2, grid_x2, grid_y2);
      return true;
    }
  }
  return false;
}

bool
Opendp::refineMove(Cell *cell)
{
  Point init = prePlaceLocation(cell, false);
  PixelPt pixel_pt = diamondSearch(cell, init.getX(), init.getY());
  if (pixel_pt.pixel) {
    double dist = abs(init.getX() - pixel_pt.pt.getX() * site_width_)
      + abs(init.getY() - pixel_pt.pt.getY() * row_height_);
    if (max_displacement_constraint_ != 0
        && (dist / row_height_ > max_displacement_constraint_)) {
      return false;
    }

    int dist_change = distChange(cell,
                                 pixel_pt.pt.getX() * site_width_,
                                 pixel_pt.pt.getY() * row_height_);

    if (dist_change < 0) {
      erasePixel(cell);
      paintPixel(cell, pixel_pt.pt.getX(), pixel_pt.pt.getY());
      return true;
    }
    return false;
  }
  return false;
}

int
Opendp::distChange(const Cell *cell, int x, int y) const
{
  Point init = prePlaceLocation(cell, false);
  int init_x = init.getX();
  int init_y = init.getY();
  int curr_dist = abs(cell->x_ - init_x) + abs(cell->y_ - init_y);
  int new_dist = abs(init_x - x) + abs(init_y - y);
  return new_dist - curr_dist;
}

////////////////////////////////////////////////////////////////

PixelPt
Opendp::diamondSearch(const Cell *cell, int x, int y) const
{
  int grid_x = gridX(x);
  int grid_y = gridY(y);
  // Restrict check to group boundary.
  Group *group = cell->group_;
  if (group) {
    Rect grid_boundary(divCeil(group->boundary.xMin(), site_width_),
                       divCeil(group->boundary.yMin(), row_height_),
                       group->boundary.xMax() / site_width_,
                       group->boundary.yMax() / row_height_);
    Point in_boundary = closestPtInRect(grid_boundary, Point(grid_x, grid_y));
    grid_x = in_boundary.x();
    grid_y = in_boundary.y();
  }

  PixelPt avail_pt = binSearch(grid_x, cell, grid_x, grid_y);
  if (avail_pt.pixel) {
    return avail_pt;
  }

  int x_start = grid_x - diamond_search_width_;
  int y_start = grid_y - diamond_search_height_;
  int x_end = grid_x + diamond_search_width_;
  int y_end = grid_y + diamond_search_height_;

#ifdef ODP_DEBUG
  cout << " == Start Diamond Search ==  " << endl;
  cout << " cell_name : " << cell->name() << endl;
  cout << " x : " << x << endl;
  cout << " y : " << y << endl;
  cout << " grid_x : " << grid_x << endl;
  cout << " grid_y : " << grid_y << endl;
  cout << " x bound ( " << x_start << ") - (" << x_end << ")" << endl;
  cout << " y bound ( " << y_start << ") - (" << y_end << ")" << endl;
#endif

  for (int i = 1; i < diamond_search_height_; i++) {
    PixelPt best_pt;
    int best_dist = 0;
    // right side
    for (int j = 1; j < i * 2; j++) {
      int x_offset = -((j + 1) / 2);
      int y_offset = (i * 2 - j) / 2;
      if (j % 2 == 1) {
        y_offset = -y_offset;
      }
      PixelPt avail_pt = binSearch(grid_x,
                                   cell,
                                   min(x_end, max(x_start, grid_x + x_offset * bin_search_width_)),
                                   min(y_end, max(y_start, grid_y + y_offset)));
      if (avail_pt.pixel) {
        int avail_dist = abs(x - avail_pt.pt.getX() * site_width_)
          + abs(y - avail_pt.pt.getY() * row_height_);
        if (best_pt.pixel == nullptr
            || avail_dist < best_dist) {
          best_pt = avail_pt;
          best_dist = avail_dist;
        }
      }
    }

    // left side
    for (int j = 1; j < (i + 1) * 2; j++) {
      int x_offset = (j - 1) / 2;
      int y_offset = ((i + 1) * 2 - j) / 2;
      if (j % 2 == 1) {
        y_offset = -y_offset;
      }
      PixelPt avail_pt = binSearch(grid_x,
                                   cell,
                                   min(x_end, max(x_start, grid_x + x_offset * bin_search_width_)),
                                   min(y_end, max(y_start, grid_y + y_offset)));
      if (avail_pt.pixel) {
        int avail_dist = abs(x - avail_pt.pt.getX() * site_width_)
          + abs(y - avail_pt.pt.getY() * row_height_);
        if (best_pt.pixel == nullptr
            || avail_dist < best_dist) {
          best_pt = avail_pt;
          best_dist = avail_dist;
        }
      }
    }
    if (best_pt.pixel) {
      return best_pt;;
    }
  }
  return PixelPt();
}

PixelPt
Opendp::binSearch(int grid_x,
                  const Cell *cell,
                  int x,
                  int y) const
{
  int x_end = x + gridPaddedWidth(cell);
  int height = gridHeight(cell);
  int y_end = y + height;

  // Check y is beyond the border.
  if (y_end > coreGridMaxY()
      // Check top power for even row multi-deck cell.
      || (height % 2 == 0 && rowTopPower(y) == topPower(cell))) {
    return PixelPt();
  }

#ifdef ODP_DEBUG
  cout << " - - - - - - - - - - - - - - - - - " << endl;
  cout << " Start Bin Search " << endl;
  cout << " cell name : " << cell->name() << endl;
  cout << " target x : " << x << endl;
  cout << " target y : " << y << endl;
#endif
  if (grid_x > x) {
    for (int i = bin_search_width_ - 1; i >= 0; i--) {
      // Check all pixels are empty.
      bool available = true;

      if (x_end + i > coreGridMaxX()) {
        available = false;
      }
      else {
        for (int k = y; k < y_end; k++) {
          for (int l = x + i; l < x_end + i; l++) {
            Pixel *pixel = gridPixel(l, k);
            if (pixel == nullptr
                || pixel->cell
                || !pixel->is_valid
                || (cell->inGroup() && pixel->group_ != cell->group_)
                || (!cell->inGroup() && pixel->group_)) {
              available = false;
              break;
            }
          }
          if (!available) {
            break;
          }
        }
      }
      if (available) {
        return PixelPt(gridPixel(x + i, y), x + i, y);
      }
    }
  }
  else {
    for (int i = 0; i < bin_search_width_; i++) {
      // check all grids are empty
      bool available = true;
      if (x_end + i > coreGridMaxX()) {
        available = false;
      }
      else {
        for (int k = y; k < y_end; k++) {
          for (int l = x + i; l < x_end + i; l++) {
            Pixel *pixel = gridPixel(l, k);
            if (pixel == nullptr
                || pixel->cell
                || !pixel->is_valid
                || (cell->inGroup() && pixel->group_ != cell->group_)
                || (!cell->inGroup() && pixel->group_)) {
              available = false;
              break;
            }
          }
        }
      }
      if (available) {
        return PixelPt(gridPixel(x + i, y), x + i, y);
      }
    }
  }
  return PixelPt();
}

////////////////////////////////////////////////////////////////

PixelPt::PixelPt() :
  pixel(nullptr)
{
}

PixelPt::PixelPt(Pixel *pixel1,
                 int grid_x,
                 int grid_y) :
  pixel(pixel1),
  pt(grid_x, grid_y)
{
}

}  // namespace opendp
