/*
 * Copyright (C) 2015-2017 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _CAMERA3_HAL_CAMERA_WINDOW_H_
#define _CAMERA3_HAL_CAMERA_WINDOW_H_

#include <utils/Errors.h>
#include <stdint.h>
#include "ia_coordinate.h"
#include "FrameInfo.h"

NAMESPACE_DECLARATION {
class CameraWindow;

/**
 * \struct Range
 * interval defined by 2 numbers.
 */
typedef struct Range {
    int start;
    int end;
    bool isEmpty() { return (start == end)? true: false; }
} Range;

/**
 * \class CameraWindow
 *  Weighted rectangle in a coordinate system where the top left corner is 0,0
 */
class CameraWindow {
public:
    CameraWindow();

    /**
     * Initializators
     */
    void init(ia_coordinate topLeft,ia_coordinate bottomRight, int aWeight);
    void init(ia_coordinate topLeft,int width, int height, int aWeight);
    void init(int aWeight);
    /**
     * Queries
     */
    bool isValid();
    ia_coordinate center(){ return mCenter; }
    int width() const { return mWidth; }
    int height() const { return mHeight; }
    int left() const { return mXleft; }
    int right() const { return mXright; }
    int top() const { return mYtop; }
    int bottom() const { return mYbottom; }
    int weight() const { return mWeight; }
    int* meteringRectangle();
    /**
     * Operations
     * This methods modify the values of the window
     */
    CameraWindow scale(float widthRatio, float heightRatio);
    // in place modification
    void clip(CameraWindow &clippingRegion);
    /**
     * Debug support
     */
    void dump();

private:
    inline void _calculateCenter();
    Range _intersect(Range range1, Range range2);

private:
    int mXleft;
    int mXright;
    int mYtop;
    int mYbottom;
    int mWeight;

    int mWidth;
    int mHeight;
    ia_coordinate mCenter;
    int mMeteringRectangle[5];
};
} NAMESPACE_DECLARATION_END

#endif // _CAMERA3_HAL_CAMERA_WINDOW_H_