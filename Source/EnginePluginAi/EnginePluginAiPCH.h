#pragma once

#include <Foundation/Basics.h>

// <StaticLinkUtil::StartHere>
// all include's before this will be left alone and not replaced by the StaticLinkUtil
// all include's AFTER this will be removed by the StaticLinkUtil and updated by what is actually used throughout the library

#include <EnginePluginAi/EnginePluginAiDLL.h>

#include <Core/World/Component.h>
#include <Core/World/GameObject.h>
#include <Core/World/World.h>
#include <EditorEngineProcessFramework/EngineProcess/EngineProcessMessages.h>
#include <Foundation/Strings/StringBuilder.h>
