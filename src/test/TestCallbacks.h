/*
 * Copyright 2014-2018 Esri R&D Zurich and VRBN
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "../palladio/Utils.h"
#include "../codec/encoder/HoudiniCallbacks.h"

#include "prt/API.h"
#include "prt/AttributeMap.h"

#include "../palladio/BoostRedirect.h"
#include PLD_BOOST_INCLUDE(/variant.hpp)

#include <vector>
#include <string>


struct CallbackResult {
	std::wstring name;
	std::vector<double> vtx;
	std::vector<double> nrm;
	std::vector<double> uvs;
	std::vector<uint32_t> cnts;
	std::vector<uint32_t> idx;
	std::vector<uint32_t> uvCnts;
	std::vector<uint32_t> uvIdx;
	uint32_t uvSets;
	std::vector<uint32_t> faceRanges;
	std::vector<AttributeMapUPtr> materials;
	std::map<int32_t, AttributeMapUPtr> attrsPerShapeID;
};

class TestCallbacks : public HoudiniCallbacks {
public:
	std::vector<CallbackResult> results;
	std::map<int32_t, AttributeMapBuilderUPtr> attrs;

	void add(const wchar_t* name,
	         const double* vtx, size_t vtxSize,
	         const double* nrm, size_t nrmSize,
	         const double* uvs, size_t uvsSize,
	         const uint32_t* counts, size_t countsSize,
	         const uint32_t* indices, size_t indicesSize,
	         const uint32_t* uvCounts, size_t uvCountsSize,
	         const uint32_t* uvIndices, size_t uvIndicesSize,
	         uint32_t uvSets,
	         const uint32_t* faceRanges, size_t faceRangesSize,
	         const prt::AttributeMap** materials,
	         const prt::AttributeMap** reports,
	         const int32_t* shapeIDs
	) override {
		results.emplace_back(CallbackResult());
		auto& cr = results.back();

		cr.name = name;
		cr.vtx.assign(vtx, vtx+vtxSize);
		cr.nrm.assign(nrm, nrm+nrmSize);
		cr.uvs.assign(uvs, uvs+uvsSize);
		cr.cnts.assign(counts, counts+countsSize);
		cr.idx.assign(indices, indices+indicesSize);
		cr.uvCnts.assign(uvCounts, uvCounts+uvCountsSize);
		cr.uvIdx.assign(uvIndices, uvIndices+uvIndicesSize);
		cr.uvSets = uvSets;

		cr.faceRanges.assign(faceRanges, faceRanges+faceRangesSize);

		for (size_t mi = 0; mi < faceRangesSize-1; mi++) {
			AttributeMapBuilderUPtr amb(prt::AttributeMapBuilder::createFromAttributeMap(materials[mi]));
			cr.materials.emplace_back(amb->createAttributeMap());
		}

		for (size_t si = 0; si < faceRangesSize-1; si++) {
			const int32_t sid = shapeIDs[si];
			cr.attrsPerShapeID.emplace(sid, AttributeMapUPtr(attrs.at(sid)->createAttributeMap()));
		}
		attrs.clear();
	}

	prt::Status generateError(size_t isIndex, prt::Status status, const wchar_t* message) override {
		return prt::STATUS_OK;
	}

	prt::Status assetError(size_t isIndex, prt::CGAErrorLevel level, const wchar_t* key, const wchar_t* uri, const wchar_t* message) override {
		return prt::STATUS_OK;
	}

	prt::Status cgaError(size_t isIndex, int32_t shapeID, prt::CGAErrorLevel level, int32_t methodId, int32_t pc, const wchar_t* message) override {
		return prt::STATUS_OK;
	}

	prt::Status cgaPrint(size_t isIndex, int32_t shapeID, const wchar_t* txt) override {
		return prt::STATUS_OK;
	}

	prt::Status cgaReportBool(size_t isIndex, int32_t shapeID, const wchar_t* key, bool value) override {
		return prt::STATUS_OK;
	}

	prt::Status cgaReportFloat(size_t isIndex, int32_t shapeID, const wchar_t* key, double value) override {
		return prt::STATUS_OK;
	}

	prt::Status cgaReportString(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* value) override {
		return prt::STATUS_OK;
	}

	prt::Status attrBool(size_t isIndex, int32_t shapeID, const wchar_t* key, bool value) override {
		auto it = attrs.emplace(shapeID, AttributeMapBuilderUPtr(prt::AttributeMapBuilder::create()));
		it.first->second->setBool(key, value);
		return prt::STATUS_OK;
	}

	prt::Status attrFloat(size_t isIndex, int32_t shapeID, const wchar_t* key, double value) override {
		auto it = attrs.emplace(shapeID, AttributeMapBuilderUPtr(prt::AttributeMapBuilder::create()));
		it.first->second->setFloat(key, value);
		return prt::STATUS_OK;
	}

	prt::Status attrString(size_t isIndex, int32_t shapeID, const wchar_t* key, const wchar_t* value) override {
		auto it = attrs.emplace(shapeID, AttributeMapBuilderUPtr(prt::AttributeMapBuilder::create()));
		it.first->second->setString(key, value);
		return prt::STATUS_OK;
	}
};
