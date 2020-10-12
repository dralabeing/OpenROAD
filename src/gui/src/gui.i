%module gui

/////////////////////////////////////////////////////////////////////////////
//
// BSD 3-Clause License
//
// Copyright (c) 2020, Matt Liberty
// All rights reserved.
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
//
///////////////////////////////////////////////////////////////////////////////

%{
#include "openroad/OpenRoad.hh"
#include "openroad/Error.hh"
#include "gui/gui.h"
%}

%inline %{

void
selection_add_net(const char* name)
{
  auto gui = gui::Gui::get();
  gui->addSelectedNet(name);
}

void
selection_add_nets(const char* name)
{
  auto gui = gui::Gui::get();
  gui->addSelectedNets(name);
}

void
selection_add_inst(const char* name)
{
  auto gui = gui::Gui::get();
  gui->addSelectedInst(name);
}

void
selection_add_insts(const char* name)
{
  auto gui = gui::Gui::get();
  gui->addSelectedInsts(name);
}

// converts from microns to DBU
void zoom_to(double xlo, double ylo, double xhi, double yhi)
{
  auto gui = gui::Gui::get();
  auto db = ord::OpenRoad::openRoad()->getDb();
  if (!db) {
    ord::warn("No database loaded");
    return;
  }
  auto chip = db->getChip();
  if (!chip) {
    ord::warn("No chip loaded");
    return;
  }
  auto block = chip->getBlock();
  if (!block) {
    ord::warn("No block loaded");
    return;
  }

  int dbuPerUU = block->getDbUnitsPerMicron();
  odb::Rect rect(xlo * dbuPerUU, ylo * dbuPerUU, xhi * dbuPerUU, yhi * dbuPerUU);
  gui->zoomTo(rect);
}

%} // inline

