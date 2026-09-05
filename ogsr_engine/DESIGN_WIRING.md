#pragma once
//--------------------------------------------------------------------------------------------------
// source_mdl_to_xray.h -- adapter declaration (see source_mdl_to_xray.cpp for docs).
//--------------------------------------------------------------------------------------------------
#include "source_mdl_skeleton.h"
#include "../bone.h"

namespace SourceMdl
{
bool BuildEngineSkeleton(const CSourceMdlSkeleton& src, vecBones& outBones, int& outRoot,
                         const Fmatrix& basis);
} // namespace SourceMdl
