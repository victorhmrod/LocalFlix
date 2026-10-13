#pragma once

#include "ApplicationController.h"
#include "ContinueCard.h"
#include "LibraryActions.h"
#include "PosterCard.h"

namespace lf::ui {

// Builds fully wired cards (click + context menu) so every screen shows library
// items the same way and routes their actions through LibraryActions.
namespace CardFactory {

PosterCard* showCard(ApplicationController& app, LibraryActions* actions, const ShowSummary& summary, QWidget* parent = nullptr);
PosterCard* movieCard(ApplicationController& app, LibraryActions* actions, const Movie& movie, QWidget* parent = nullptr);
// Card for a recently-added entry (show or movie).
PosterCard* libraryCard(ApplicationController& app, LibraryActions* actions, const LibraryCard& card, QWidget* parent = nullptr);
ContinueCard* continueCard(ApplicationController& app, LibraryActions* actions, const ContinueItem& item, QWidget* parent = nullptr);

}  // namespace CardFactory

}  // namespace lf::ui
