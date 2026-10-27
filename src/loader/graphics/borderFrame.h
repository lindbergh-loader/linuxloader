#pragma once

// The light gun border (if border is set) and the frame overlay (see
// border.h), drawn on the window's back buffer right before the
// swap. glad must be loaded for the current context.
//
// Some games read the previous frame back from the window's back buffer
// (Terminator Salvation's levels): the border would end up in their
// picture. The frame is kept, clean, in a renderbuffer while the border is
// drawn and presented, and borderFrameEnd() puts it back after the swap.
void borderFrameBegin(int width, int height, int border, float whiteBorderPercentage, float blackBorderPercentage);
void borderFrameEnd(void);
