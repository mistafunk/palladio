#include "client/sop.h"
#include "client/callbacks.h"

#include "prt/API.h"
#include "prt/FlexLicParams.h"

#ifndef WIN32
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wunused-local-typedefs"
#	pragma GCC diagnostic ignored "-Wattributes"
#endif
#include "GU/GU_Detail.h"
#include "GU/GU_PrimPoly.h"
#include "PRM/PRM_Include.h"
#include "PRM/PRM_SpareData.h"
#include "PRM/PRM_ChoiceList.h"
#include "PRM/PRM_Parm.h"
#include "OP/OP_Operator.h"
#include "OP/OP_OperatorTable.h"
#include "OP/OP_Director.h"
#include "SOP/SOP_Guide.h"
#include "GOP/GOP_GroupParse.h"
#include "GEO/GEO_PolyCounts.h"
#include "PI/PI_EditScriptedParms.h"
#include "UT/UT_DSOVersion.h"
#include "UT/UT_Matrix3.h"
#include "UT/UT_Matrix4.h"
#include "UT/UT_Exit.h"
#include "UT/UT_Interrupt.h"
#include "SYS/SYS_Math.h"
#include <HOM/HOM_Module.h>
#include <HOM/HOM_SopNode.h>

#include "boost/foreach.hpp"
#include "boost/filesystem.hpp"
#include "boost/algorithm/string.hpp"

#ifdef WIN32
#	include "boost/assign.hpp"
#endif

#ifndef WIN32
#	pragma GCC diagnostic pop
#endif

#include <vector>
#include <cwchar>


namespace {

const bool DBG = false;

// global prt settings
const prt::LogLevel	PRT_LOG_LEVEL		= prt::LOG_DEBUG;
const char*			PRT_LIB_SUBDIR		= "prtlib";
const char*			FILE_FLEXNET_LIB	= "flexnet_prt";
const wchar_t*		FILE_CGA_ERROR		= L"CGAErrors.txt";
const wchar_t*		FILE_CGA_PRINT		= L"CGAPrint.txt";

// some encoder IDs
const wchar_t*	ENCODER_ID_CGA_EVALATTR	= L"com.esri.prt.core.AttributeEvalEncoder";
const wchar_t*	ENCODER_ID_CGA_ERROR	= L"com.esri.prt.core.CGAErrorEncoder";
const wchar_t*	ENCODER_ID_CGA_PRINT	= L"com.esri.prt.core.CGAPrintEncoder";
const wchar_t*	ENCODER_ID_HOUDINI		= L"HoudiniEncoder";

// objects with same lifetime as PRT process
const prt::Object* prtLicHandle = 0;

void dsoExit(void*) {
	if (prtLicHandle)
		prtLicHandle->destroy(); // prt shutdown
}

PRM_Name NODE_PARAM_SHAPE_CLS_ATTR	("shapeClsAttr",	"Shape Classifier");
PRM_Name NODE_PARAM_SHAPE_CLS_TYPE	("shapeClsType",	"Shape Classifier Type");
PRM_Name NODE_PARAM_RPK				("rpk",				"Rule Package");
PRM_Name NODE_PARAM_RULE_FILE		("ruleFile", 		"Rule File");
PRM_Name NODE_PARAM_STYLE			("style",			"Style");
PRM_Name NODE_PARAM_START_RULE		("startRule",		"Start Rule");
PRM_Name NODE_PARAM_SEED			("seed",			"Random Seed");
PRM_Name NODE_PARAM_LOG				("logLevel",		"Log Level");

PRM_Name NODE_MULTIPARAM_FLOAT_NUM	("cgaFltNum",		"Number of Float Attributes");
PRM_Name NODE_MULTIPARAM_FLOAT_ATTR	("cgaFltAttr#",		"Float Attr #");
PRM_Name NODE_MULTIPARAM_FLOAT_VAL	("cgaFltVal#",		"Float Value #");
PRM_Name NODE_MULTIPARAM_FLOAT_RESET("cgaFltReset#",	"Reset to Rule");
PRM_Name NODE_MULTIPARAM_STRING_NUM	("cgaStrNum",		"Number of String Attributes");
PRM_Name NODE_MULTIPARAM_STRING_ATTR("cgaStrAttr#",		"String Attr #");
PRM_Name NODE_MULTIPARAM_STRING_VAL	("cgaStrVal#",		"String Value #");
PRM_Name NODE_MULTIPARAM_STRING_RESET("cgaStrReset#",	"Reset to Rule");
PRM_Name NODE_MULTIPARAM_BOOL_NUM	("cgaBoolNum",		"Number of Boolean Attributes");
PRM_Name NODE_MULTIPARAM_BOOL_ATTR	("cgaBoolAttr#",	"Boolean Attr #");
PRM_Name NODE_MULTIPARAM_BOOL_VAL	("cgaBoolVal#",		"Boolan Value #");
PRM_Name NODE_MULTIPARAM_BOOL_RESET	("cgaBoolReset#",	"Reset to Rule");

PRM_Default rpkDefault(0, "$HIP/$F.rpk");

PRM_ChoiceList ruleFileMenu(static_cast<PRM_ChoiceListType>(PRM_CHOICELIST_EXCLUSIVE | PRM_CHOICELIST_REPLACE), &p4h::SOP_PRT::buildRuleFileMenu);
PRM_ChoiceList startRuleMenu(static_cast<PRM_ChoiceListType>(PRM_CHOICELIST_EXCLUSIVE | PRM_CHOICELIST_REPLACE), &p4h::SOP_PRT::buildStartRuleMenu);

PRM_Name shapeClsTypes[] = {
		PRM_Name("STRING", "String"),
		PRM_Name("INT", "Integer"),
		PRM_Name("FLOAT", "Float"),
		PRM_Name(0)
};
PRM_ChoiceList shapeClsTypeMenu((PRM_ChoiceListType)(PRM_CHOICELIST_EXCLUSIVE | PRM_CHOICELIST_REPLACE), shapeClsTypes);
PRM_Default shapeClsTypeDefault(0, "INT");

PRM_Name logNames[] = {
		PRM_Name("TRACE", "trace"), // TODO: eventually, remove this and offset index by 1
		PRM_Name("DEBUG", "debug"),
		PRM_Name("INFO", "info"),
		PRM_Name("WARNING", "warning"),
		PRM_Name("ERROR", "error"),
		PRM_Name("FATAL", "fatal"),
		PRM_Name(0)
};
PRM_ChoiceList logMenu((PRM_ChoiceListType)(PRM_CHOICELIST_EXCLUSIVE | PRM_CHOICELIST_REPLACE), logNames);
PRM_Default logDefault(0, "ERROR");

int	resetRuleAttr(void *data, int index, fpreal64 time, const PRM_Template *tplate) {
	p4h::SOP_PRT* node = static_cast<p4h::SOP_PRT*>(data);

	UT_String tok;
	tplate->getToken(tok);
	const char* suf = tok.suffix();
	LOG_DBG << "resetRuleAttr: tok = " << tok.toStdString() << ", suf = " << suf;
	UT_String ruleAttr;
	if (suf) {
		int idx = std::atoi(suf);
		const char* valueTok = nullptr;
		if (tok.startsWith("cgaFlt"))
			valueTok = NODE_MULTIPARAM_FLOAT_ATTR.getToken();
		else if (tok.startsWith("cgaStr"))
			valueTok = NODE_MULTIPARAM_STRING_ATTR.getToken();
		else if (tok.startsWith("cgaBool"))
			valueTok = NODE_MULTIPARAM_BOOL_ATTR.getToken();
		if (valueTok)
			node->evalStringInst(valueTok, &idx, ruleAttr, 0, 0.0);
		LOG_DBG << "    idx = " << idx << ", valueTok = " << valueTok << ", ruleAttr = " << ruleAttr;
	}
	if (ruleAttr.length() > 0)
		node->resetUserAttribute(ruleAttr.toStdString());
	return 1;
}

PRM_Template NODE_MULTIPARAM_FLOAT_ATTR_TEMPLATE[] = {
		PRM_Template(PRM_STRING, 1, &NODE_MULTIPARAM_FLOAT_ATTR, PRMoneDefaults),
		PRM_Template(PRM_FLT, 1, &NODE_MULTIPARAM_FLOAT_VAL, PRMoneDefaults),
		PRM_Template(PRM_CALLBACK_NOREFRESH, 1, &NODE_MULTIPARAM_FLOAT_RESET, PRMoneDefaults, nullptr, nullptr, PRM_Callback(&resetRuleAttr)),
		PRM_Template()
};

PRM_Template NODE_MULTIPARAM_STRING_ATTR_TEMPLATE[] = {
		PRM_Template(PRM_STRING, 1, &NODE_MULTIPARAM_STRING_ATTR, PRMoneDefaults),
		PRM_Template(PRM_STRING, 1, &NODE_MULTIPARAM_STRING_VAL, PRMoneDefaults),
		PRM_Template(PRM_CALLBACK_NOREFRESH, 1, &NODE_MULTIPARAM_STRING_RESET, PRMoneDefaults, nullptr, nullptr, PRM_Callback(&resetRuleAttr)),
		PRM_Template()
};

PRM_Template NODE_MULTIPARAM_BOOL_ATTR_TEMPLATE[] = {
		PRM_Template(PRM_STRING, 1, &NODE_MULTIPARAM_BOOL_ATTR, PRMoneDefaults),
		PRM_Template(PRM_TOGGLE, 1, &NODE_MULTIPARAM_BOOL_VAL, PRMoneDefaults),
		PRM_Template(PRM_CALLBACK_NOREFRESH, 1, &NODE_MULTIPARAM_BOOL_RESET, PRMoneDefaults, nullptr, nullptr, PRM_Callback(&resetRuleAttr)),
		PRM_Template()
};

PRM_Template NODE_PARAM_TEMPLATES[] = {
		PRM_Template(PRM_STRING, 1, &NODE_PARAM_SHAPE_CLS_ATTR, PRMoneDefaults),
		PRM_Template(PRM_ORD, PRM_Template::PRM_EXPORT_MAX, 1, &NODE_PARAM_SHAPE_CLS_TYPE, &shapeClsTypeDefault, &shapeClsTypeMenu),
		PRM_Template(PRM_FILE, 1, &NODE_PARAM_RPK, &rpkDefault, nullptr, nullptr, 0, &PRM_SpareData::fileChooserModeRead),
		PRM_Template(PRM_STRING, 1, &NODE_PARAM_RULE_FILE, PRMoneDefaults, &ruleFileMenu),
		PRM_Template(PRM_STRING, 1, &NODE_PARAM_STYLE, PRMoneDefaults),
		PRM_Template(PRM_STRING, 1, &NODE_PARAM_START_RULE, PRMoneDefaults, &startRuleMenu),
		PRM_Template(PRM_INT, 1, &NODE_PARAM_SEED, PRMoneDefaults),
		PRM_Template(PRM_ORD, PRM_Template::PRM_EXPORT_MAX, 1, &NODE_PARAM_LOG, &logDefault, &logMenu),

		PRM_Template((PRM_MultiType)(PRM_MULTITYPE_LIST | PRM_MULTITYPE_NO_CONTROL_UI), NODE_MULTIPARAM_FLOAT_ATTR_TEMPLATE, 1, &NODE_MULTIPARAM_FLOAT_NUM, PRMoneDefaults, nullptr, &PRM_SpareData::multiStartOffsetZero),
		PRM_Template((PRM_MultiType)(PRM_MULTITYPE_LIST | PRM_MULTITYPE_NO_CONTROL_UI), NODE_MULTIPARAM_STRING_ATTR_TEMPLATE, 1, &NODE_MULTIPARAM_STRING_NUM, PRMoneDefaults, nullptr, &PRM_SpareData::multiStartOffsetZero),
		PRM_Template((PRM_MultiType)(PRM_MULTITYPE_LIST | PRM_MULTITYPE_NO_CONTROL_UI), NODE_MULTIPARAM_BOOL_ATTR_TEMPLATE, 1, &NODE_MULTIPARAM_BOOL_NUM, PRMoneDefaults, nullptr, &PRM_SpareData::multiStartOffsetZero),

		PRM_Template()
};

} // namespace anonymous


// TODO: add support for multiple nodes
void newSopOperator(OP_OperatorTable *table) {
	UT_Exit::addExitCallback(dsoExit);

	boost::filesystem::path sopPath;
	p4h::utils::getPathToCurrentModule(sopPath);

	prt::FlexLicParams flp;
	std::string libflexnet = p4h::utils::getSharedLibraryPrefix() + FILE_FLEXNET_LIB + p4h::utils::getSharedLibrarySuffix();
	std::string libflexnetPath = (sopPath.parent_path() / libflexnet).string();
	flp.mActLibPath = libflexnetPath.c_str();
	flp.mFeature = "CityEngAdvFx";
	flp.mHostName = "";

	std::wstring libPath = (sopPath.parent_path() / PRT_LIB_SUBDIR).wstring();
	const wchar_t* extPaths[] = { libPath.c_str() };

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	prtLicHandle = prt::init(extPaths, 1, PRT_LOG_LEVEL, &flp, &status); // TODO: add UI for log level control

	if (prtLicHandle == 0 || status != prt::STATUS_OK)
		return;

	const size_t minSources = 1;
	const size_t maxSources = 1;
	table->addOperator(new OP_Operator("prt4houdini", "prt4houdini", p4h::SOP_PRT::create,
			NODE_PARAM_TEMPLATES, minSources, maxSources, 0)
	);
}


namespace p4h {

OP_Node* SOP_PRT::create(OP_Network *net, const char *name, OP_Operator *op) {
	return new SOP_PRT(net, name, op);
}

SOP_PRT::SOP_PRT(OP_Network *net, const char *name, OP_Operator *op)
: SOP_Node(net, name, op)
, mPRTCache(prt::CacheObject::create(prt::CacheObject::CACHE_TYPE_DEFAULT))
, mLogHandler(new log::LogHandler(utils::toUTF16FromOSNarrow(name), prt::LOG_ERROR))
{
	prt::addLogHandler(mLogHandler.get());

	AttributeMapBuilderPtr optionsBuilder(prt::AttributeMapBuilder::create());

	const prt::AttributeMap* encoderOptions = optionsBuilder->createAttributeMapAndReset();
	mHoudiniEncoderOptions = utils::createValidatedOptions(ENCODER_ID_HOUDINI, encoderOptions);
	encoderOptions->destroy();

	optionsBuilder->setString(L"name", FILE_CGA_ERROR);
	const prt::AttributeMap* errOptions = optionsBuilder->createAttributeMapAndReset();
	mCGAErrorOptions = utils::createValidatedOptions(ENCODER_ID_CGA_ERROR, errOptions);
	errOptions->destroy();

	optionsBuilder->setString(L"name", FILE_CGA_PRINT);
	const prt::AttributeMap* printOptions = optionsBuilder->createAttributeMapAndReset();
	mCGAPrintOptions = utils::createValidatedOptions(ENCODER_ID_CGA_PRINT, printOptions);
	printOptions->destroy();

#ifdef WIN32
	mAllEncoders = boost::assign::list_of(ENCODER_ID_HOUDINI)(ENCODER_ID_CGA_ERROR)(ENCODER_ID_CGA_PRINT);
	mAllEncoderOptions = boost::assign::list_of(mHoudiniEncoderOptions)(mCGAErrorOptions)(mCGAPrintOptions);
#else
	mAllEncoders = { ENCODER_ID_HOUDINI, ENCODER_ID_CGA_ERROR, ENCODER_ID_CGA_PRINT };
	mAllEncoderOptions = { mHoudiniEncoderOptions, mCGAErrorOptions, mCGAPrintOptions };
#endif
}

SOP_PRT::~SOP_PRT() {
	mHoudiniEncoderOptions->destroy();
	mCGAErrorOptions->destroy();
	mCGAPrintOptions->destroy();

	mPRTCache->destroy();
	prt::removeLogHandler(mLogHandler.get());
}

OP_ERROR SOP_PRT::cookMySop(OP_Context &context) {
	LOG_DBG << "cookMySop";

	if (!handleParams(context))
		return UT_ERROR_ABORT;

	if (lockInputs(context) >= UT_ERROR_ABORT) {
		LOG_DBG << "lockInputs error";
		return error();
	}

	duplicateSource(0, context);

	if (error() < UT_ERROR_ABORT && cookInputGroups(context) < UT_ERROR_ABORT) {
		UT_AutoInterrupt progress("Generating CityEngine Geometry...");

		InitialShapeGenerator isc(gdp, mInitialShapeContext);
		gdp->clearAndDestroy();
		{
			HoudiniGeometry hg(gdp);
			// TODO: add occlusion
			InitialShapeNOPtrVector& initialShapes = isc.getInitialShapes();
			prt::Status stat = prt::generate(
					initialShapes.data(), initialShapes.size(), 0,
					mAllEncoders.data(), mAllEncoders.size(), mAllEncoderOptions.data(),
					&hg, mPRTCache, nullptr
			);
			if(stat != prt::STATUS_OK) {
				LOG_ERR << "prt::generate() failed with status: '" << prt::getStatusDescription(stat) << "' (" << stat << ")";
			}
		}
		select(GU_SPrimitive);
	}

	unlockInputs();

	LOG_DBG << "cookMySop done";
	return error();
}

namespace {

namespace UnitQuad {
const double 	vertices[]				= { 0, 0, 0,  0, 0, 1,  1, 0, 1,  1, 0, 0 };
const size_t 	vertexCount				= 12;
const uint32_t	indices[]				= { 0, 1, 2, 3 };
const size_t 	indexCount				= 4;
const uint32_t 	faceCounts[]			= { 4 };
const size_t 	faceCountsCount			= 1;
}

} // anonymous namespace

bool SOP_PRT::handleParams(OP_Context &context) {
	LOG_DBG << "handleParams begin";
	fpreal now = context.getTime();

	// -- shape classifier attr name
	evalString(mInitialShapeContext.mShapeClsAttrName, NODE_PARAM_SHAPE_CLS_ATTR.getToken(), 0, now);

	// -- shape classifier attr type
	int shapeClsAttrTypeChoice = evalInt(NODE_PARAM_SHAPE_CLS_TYPE.getToken(), 0, now);
	if (shapeClsAttrTypeChoice == 0)
		mInitialShapeContext.mShapeClsType = GA_STORECLASS_STRING;
	else if (shapeClsAttrTypeChoice == 1)
		mInitialShapeContext.mShapeClsType = GA_STORECLASS_INT;
	else if (shapeClsAttrTypeChoice == 2)
		mInitialShapeContext.mShapeClsType = GA_STORECLASS_FLOAT;

	// -- rule package
	UT_String utNextRPKStr;
	evalString(utNextRPKStr, NODE_PARAM_RPK.getToken(), 0, now);
	boost::filesystem::path nextRPK(utNextRPKStr.toStdString());
	if (!updateRulePackage(nextRPK, now)) {
		const PRM_Parm& p = getParm(NODE_PARAM_RPK.getToken());
		UT_String expr;
		p.getExpressionOnly(now, expr, 0, 0);
		if (expr.length() == 0) { // if not an expression ...
			UT_String val(mInitialShapeContext.mRPK.string());
			setString(val, CH_STRING_LITERAL, NODE_PARAM_RPK.getToken(), 0, now); // ... reset to current value
		}
		return false;
	}

	// -- rule file
	UT_String utRuleFile;
	evalString(utRuleFile, NODE_PARAM_RULE_FILE.getToken(), 0, now);
	mInitialShapeContext.mRuleFile = utils::toUTF16FromOSNarrow(utRuleFile.toStdString());
	LOG_DBG << L"got rule file: " << mInitialShapeContext.mRuleFile;

	// -- style
	UT_String utStyle;
	evalString(utStyle, NODE_PARAM_STYLE.getToken(), 0, now);
	mInitialShapeContext.mStyle = utils::toUTF16FromOSNarrow(utStyle.toStdString());

	// -- start rule
	UT_String utStartRule;
	evalString(utStartRule, NODE_PARAM_START_RULE.getToken(), 0, now);
	mInitialShapeContext.mStartRule = utils::toUTF16FromOSNarrow(utStartRule.toStdString());

	// -- random seed
	mInitialShapeContext.mSeed = evalInt(NODE_PARAM_SEED.getToken(), 0, now);

	// -- logger
	prt::LogLevel ll = static_cast<prt::LogLevel>(evalInt(NODE_PARAM_LOG.getToken(), 0, now));
	mLogHandler->setLevel(ll);
	mLogHandler->setName(utils::toUTF16FromOSNarrow(getName().toStdString()));

	LOG_DBG << "handleParams done.";
	return true;
}

namespace {

void getCGBs(const ResolveMapPtr& rm, std::vector<std::pair<std::wstring,std::wstring>>& cgbs) {
	static const wchar_t*	PROJECT		= L"";
	static const wchar_t*	PATTERN		= L"*.cgb";
	static const size_t		START_SIZE	= 16 * 1024;

	size_t resultSize = START_SIZE;
	wchar_t* result = new wchar_t[resultSize];
	rm->searchKey(PROJECT, PATTERN, result, &resultSize);
	if (resultSize >= START_SIZE) {
		delete[] result;
		result = new wchar_t[resultSize];
		rm->searchKey(PROJECT, PATTERN, result, &resultSize);
	}
	std::wstring cgbList(result);
	delete[] result;
	LOG_DBG << "   cgbList = '" << cgbList << "'";

	std::vector<std::wstring> tok;
	boost::split(tok, cgbList, boost::is_any_of(L";"), boost::algorithm::token_compress_on);
	for(const std::wstring& t: tok) {
		if (t.empty()) continue;
		LOG_DBG << "token: '" << t << "'";
		const wchar_t* s = rm->getString(t.c_str());
		if (s != nullptr) {
			cgbs.emplace_back(t, s);
			LOG_DBG << L"got cgb: " << cgbs.back().first << L" => " << cgbs.back().second;
		}
	}
}

void getDefaultRuleAttributeValues(
		AttributeMapBuilderPtr& amb,
		prt::Cache* cache,
		const ResolveMapPtr& resolveMap,
		const std::wstring& cgbKey,
		const std::wstring& startRule
) {
	HoudiniGeometry hg(nullptr, amb.get());
	AttributeMapPtr emptyAttrMap{amb->createAttributeMapAndReset()};

	InitialShapeBuilderPtr isb{prt::InitialShapeBuilder::create()};
	isb->setGeometry(UnitQuad::vertices, UnitQuad::vertexCount, UnitQuad::indices, UnitQuad::indexCount, UnitQuad::faceCounts, UnitQuad::faceCountsCount);
	isb->setAttributes(cgbKey.c_str(), startRule.c_str(), 666, L"temp", emptyAttrMap.get(), resolveMap.get());

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	InitialShapePtr is{isb->createInitialShapeAndReset(&status)};
	const prt::InitialShape* iss[1] = { is.get() };

	EncoderInfoPtr encInfo{prt::createEncoderInfo(ENCODER_ID_CGA_EVALATTR)};
	const prt::AttributeMap* encOpts = nullptr;
	encInfo->createValidatedOptionsAndStates(nullptr, &encOpts);

	const wchar_t* encs[1] = { ENCODER_ID_CGA_EVALATTR };
	const prt::AttributeMap* encsOpts[1] = { encOpts };

	prt::Status stat = prt::generate(iss, 1, nullptr, encs, 1, encsOpts, &hg, cache, nullptr, nullptr);
	if(stat != prt::STATUS_OK) {
		LOG_ERR << "prt::generate() failed with status: '" << prt::getStatusDescription(stat) << "' (" << stat << ")";
	}

	encOpts->destroy();
}

} // anonymous namespace

bool SOP_PRT::updateRulePackage(const boost::filesystem::path& nextRPK, fpreal time) {
	if (nextRPK == mInitialShapeContext.mRPK)
		return true;
	if (!boost::filesystem::exists(nextRPK))
		return false;

	std::wstring nextRPKURI = utils::toFileURI(nextRPK);
	LOG_DBG << L"nextRPKURI = " << nextRPKURI;

	// rebuild assets map
	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	boost::filesystem::path unpackPath = boost::filesystem::temp_directory_path();
	ResolveMapPtr nextResolveMap(prt::createResolveMap(nextRPKURI.c_str(), unpackPath.wstring().c_str(), &status));
	if (!nextResolveMap || status != prt::STATUS_OK) {
		LOG_ERR << "failed to create resolve map from '" << nextRPKURI << "', aborting.";
		return false;
	}

	// get first rule file
	std::vector<std::pair<std::wstring,std::wstring>> cgbs; // key -> uri
	getCGBs(nextResolveMap, cgbs);
	if (cgbs.empty()) {
		LOG_ERR << "no rule files found in rule package";
		return false;
	}
	std::wstring cgbKey = cgbs.front().first;
	std::wstring cgbURI = cgbs.front().second;
	LOG_DBG << "cgbKey = " << cgbKey;
	LOG_DBG << "cgbURI = " << cgbURI;

	status = prt::STATUS_UNSPECIFIED_ERROR;
	RuleFileInfoPtr info(prt::createRuleFileInfo(cgbURI.c_str(), mPRTCache, &status));
	if (!info || status != prt::STATUS_OK) {
		LOG_ERR << "failed to get rule file info";
		return false;
	}

	// get first rule (start rule if possible)
	if (info->getNumRules() == 0) {
		LOG_ERR << "rule file does not contain any rules";
		return false;
	}
	size_t startRuleIdx = size_t(-1);
	for (size_t ri = 0; ri < info->getNumRules(); ri++) {
		const prt::RuleFileInfo::Entry* re = info->getRule(ri);
		for (size_t ai = 0; ai < re->getNumAnnotations(); ai++) {
			if (std::wcscmp(re->getAnnotation(ai)->getName(), L"@StartRule") == 0) {
				startRuleIdx = ri;
				break;
			}
		}
	}
	if (startRuleIdx == size_t(-1))
		startRuleIdx = 0;
	const prt::RuleFileInfo::Entry* re = info->getRule(startRuleIdx);
	std::wstring fqStartRule = re->getName();
	std::vector<std::wstring> startRuleComponents;
	boost::split(startRuleComponents, fqStartRule, boost::is_any_of(L"$"));
	LOG_DBG << "first start rule: " << fqStartRule;

	// update node
	mInitialShapeContext.mRPK = nextRPK;
	mInitialShapeContext.mAssetsMap.swap(nextResolveMap);
	mPRTCache->flushAll();
	{
		mInitialShapeContext.mRuleFile = cgbKey;
		UT_String val(utils::toOSNarrowFromUTF16(mInitialShapeContext.mRuleFile));
		setString(val, CH_STRING_LITERAL, NODE_PARAM_RULE_FILE.getToken(), 0, time);
	}
	{
		mInitialShapeContext.mStyle = startRuleComponents[0];
		UT_String val(utils::toOSNarrowFromUTF16(mInitialShapeContext.mStyle));
		setString(val, CH_STRING_LITERAL, NODE_PARAM_STYLE.getToken(), 0, time);
	}
	{
		mInitialShapeContext.mStartRule = startRuleComponents[1];
		UT_String val(utils::toOSNarrowFromUTF16(mInitialShapeContext.mStartRule));
		setString(val, CH_STRING_LITERAL, NODE_PARAM_START_RULE.getToken(), 0, time);
	}
	{
		mInitialShapeContext.mSeed = 0;
		setInt(NODE_PARAM_SEED.getToken(), 0, time, mInitialShapeContext.mSeed);
	}
	LOG_DBG << "updateRulePackage done: mRuleFile = " << mInitialShapeContext.mRuleFile << ", mStyle = " << mInitialShapeContext.mStyle << ", mStartRule = " << mInitialShapeContext.mStartRule;

	// eval rule attribute values
	AttributeMapBuilderPtr amb{prt::AttributeMapBuilder::create()};
	getDefaultRuleAttributeValues(amb, mPRTCache, mInitialShapeContext.mAssetsMap, mInitialShapeContext.mRuleFile, fqStartRule);
	mInitialShapeContext.mRuleAttributeValues.reset(amb->createAttributeMap());
	mInitialShapeContext.mUserAttributeValues.reset(amb->createAttributeMap()); // pristine user attribute values
	LOG_DBG << "mRuleAttributeValues = " << utils::objectToXML(mInitialShapeContext.mRuleAttributeValues.get());

	// update rule attribute UI
	createMultiParams(info, mInitialShapeContext.mRuleFile, fqStartRule, time);

	return true;
}

void SOP_PRT::createMultiParams(
		const RuleFileInfoPtr& info,
		const std::wstring& cgbKey,
		const std::wstring& fqStartRule,
		fpreal time
) {
	size_t keyCount = 0;
	const wchar_t* const* cKeys = mInitialShapeContext.mRuleAttributeValues->getKeys(&keyCount);

	size_t numFlt = 0, numStr = 0, numBool = 0;
	for (size_t k = 0; k < keyCount; k++) {
		const wchar_t* key = cKeys[k];
		switch (mInitialShapeContext.mRuleAttributeValues->getType(key)) {
			case prt::AttributeMap::PT_FLOAT:
				numFlt++;
				break;
			case prt::AttributeMap::PT_STRING:
				numStr++;
				break;
			case prt::AttributeMap::PT_BOOL:
				numBool++;
				break;
			default:
				break;
		}
	}

	setInt(NODE_MULTIPARAM_FLOAT_NUM.getToken(), 0, 0, numFlt);
	setInt(NODE_MULTIPARAM_STRING_NUM.getToken(), 0, 0, numStr);
	setInt(NODE_MULTIPARAM_BOOL_NUM.getToken(), 0, 0, numBool);

	int idxFlt = 0, idxStr = 0, idxBool = 0;
	for (size_t k = 0; k < keyCount; k++) {
		const wchar_t* key = cKeys[k];
		std::string nKey = utils::toOSNarrowFromUTF16(key);
		std::string nLabel = nKey.substr(nKey.find_first_of('$')+1); // strip style
		switch (mInitialShapeContext.mRuleAttributeValues->getType(key)) {
		case prt::AttributeMap::PT_FLOAT: {
			double v = mInitialShapeContext.mRuleAttributeValues->getFloat(key);

			setStringInst(UT_String(nLabel), CH_STRING_LITERAL, NODE_MULTIPARAM_FLOAT_ATTR.getToken(), &idxFlt, 0, time);
			PRM_Parm* p = getParmPtrInst(NODE_MULTIPARAM_FLOAT_ATTR.getToken(), &idxFlt);
			if (p) p->setLockedFlag(0, true);

			setFloatInst(v, NODE_MULTIPARAM_FLOAT_VAL.getToken(), &idxFlt, 0, time);

			idxFlt++;
			break;
		}
		case prt::AttributeMap::PT_STRING: {
			const wchar_t* v = mInitialShapeContext.mRuleAttributeValues->getString(key);
			std::string nv = utils::toOSNarrowFromUTF16(v);

			setStringInst(UT_String(nLabel), CH_STRING_LITERAL, NODE_MULTIPARAM_STRING_ATTR.getToken(), &idxStr, 0, time);
			PRM_Parm* p = getParmPtrInst(NODE_MULTIPARAM_STRING_ATTR.getToken(), &idxFlt);
			if (p) p->setLockedFlag(0, true);

			setStringInst(UT_String(nv), CH_STRING_LITERAL, NODE_MULTIPARAM_STRING_VAL.getToken(), &idxStr, 0, time);

			idxStr++;
			break;
		}
		case prt::AttributeMap::PT_BOOL: {
			bool v = mInitialShapeContext.mRuleAttributeValues->getBool(key);

			setStringInst(UT_String(nLabel), CH_STRING_LITERAL, NODE_MULTIPARAM_BOOL_ATTR.getToken(), &idxBool, 0, time);
			PRM_Parm* p = getParmPtrInst(NODE_MULTIPARAM_BOOL_ATTR.getToken(), &idxFlt);
			if (p) p->setLockedFlag(0, true);

			setIntInst(v ? 1 : 0, NODE_MULTIPARAM_BOOL_VAL.getToken(), &idxBool, 0, time);

			idxBool++;
			break;
		}
		default: {
			LOG_WRN << "attribute " << nKey << ": type not handled";
			break;
		}
		}
	}
}

bool SOP_PRT::updateParmsFlags() {
	// TODO: maybe better to do all of below in createInitialShapes (less distributed state)?
	if (mInitialShapeContext.mRuleAttributeValues) {
		AttributeMapBuilderPtr amb{prt::AttributeMapBuilder::createFromAttributeMap(mInitialShapeContext.mRuleAttributeValues.get())};

		int idxFlt = 0, idxStr = 0, idxBool = 0;
		size_t keyCount = 0;
		const wchar_t* const* cKeys = mInitialShapeContext.mRuleAttributeValues->getKeys(&keyCount);
		for (size_t k = 0; k < keyCount; k++) {
			const wchar_t* key = cKeys[k];
			switch (mInitialShapeContext.mRuleAttributeValues->getType(key)) {
				case prt::AttributeMap::PT_FLOAT: {
					double v = evalFloatInst(NODE_MULTIPARAM_FLOAT_VAL.getToken(), &idxFlt, 0, 0.0); // TODO: time
					amb->setFloat(key, v);
					idxFlt++;
					break;
				}
				case prt::AttributeMap::PT_STRING: {
					UT_String v;
					evalStringInst(NODE_MULTIPARAM_STRING_VAL.getToken(), &idxStr, v, 0, 0.0);
					std::wstring wv = utils::toUTF16FromOSNarrow(v.toStdString());
					amb->setString(key, wv.c_str());
					idxStr++;
					break;
				}
				case prt::AttributeMap::PT_BOOL: {
					bool v = (evalIntInst(NODE_MULTIPARAM_BOOL_VAL.getToken(), &idxBool, 0, 0.0) > 0);
					amb->setBool(key, v);
					idxBool++;
					break;
				}
				default: {
					LOG_WRN << L"attribute " << key << L": type not handled";
					break;
				}
			}
		}

		mInitialShapeContext.mUserAttributeValues.reset(amb->createAttributeMap());
		forceRecook();
	}
	return SOP_Node::updateParmsFlags();
}

void SOP_PRT::resetUserAttribute(const std::string& token) {
	size_t keyCount = 0;
	const wchar_t* const* cKeys = mInitialShapeContext.mRuleAttributeValues->getKeys(&keyCount);

	int idxFlt = 0, idxStr = 0, idxBool = 0;
	for (size_t k = 0; k < keyCount; k++) {
		const wchar_t* key = cKeys[k];
		std::string nKey = utils::toOSNarrowFromUTF16(key);
		std::string nLabel = nKey.substr(nKey.find_first_of('$')+1); // strip style

		switch (mInitialShapeContext.mRuleAttributeValues->getType(key)) {
			case prt::AttributeMap::PT_FLOAT: {
				if (nLabel.compare(token) == 0) {
					double v = mInitialShapeContext.mRuleAttributeValues->getFloat(key);
					setFloatInst(v, NODE_MULTIPARAM_FLOAT_VAL.getToken(), &idxFlt, 0, 0.0);
				}
				idxFlt++;
				break;
			}
			case prt::AttributeMap::PT_STRING: {
				if (nLabel.compare(token) == 0) {
					const wchar_t* v = mInitialShapeContext.mRuleAttributeValues->getString(key);
					std::string nv = utils::toOSNarrowFromUTF16(v);
					setStringInst(UT_String(nv), CH_STRING_LITERAL, NODE_MULTIPARAM_STRING_VAL.getToken(), &idxStr, 0, 0.0);
				}
				idxStr++;
				break;
			}
			case prt::AttributeMap::PT_BOOL: {
				if (nLabel.compare(token) == 0) {
					bool v = mInitialShapeContext.mRuleAttributeValues->getBool(key);
					setIntInst(v ? 1 : 0, NODE_MULTIPARAM_BOOL_VAL.getToken(), &idxBool, 0, 0.0);
				}
				idxBool++;
				break;
			}
			default: {
				LOG_WRN << "attribute " << nKey << ": type not handled";
				break;
			}
		}
	}
}


namespace {

typedef std::vector<std::pair<std::string,std::string>> StringPairVector;
bool compareSecond (const StringPairVector::value_type& a, const StringPairVector::value_type& b) { return ( a.second < b.second ); }

} // namespace

void SOP_PRT::buildStartRuleMenu(void* data, PRM_Name* theMenu, int theMaxSize, const PRM_SpareData*, const PRM_Parm*) {
	static const bool DBG = false;

	SOP_PRT* node = static_cast<SOP_PRT*>(data);
	if (DBG) {
		LOG_DBG << "buildStartRuleMenu";
		LOG_DBG << "   mRPK = " << node->mInitialShapeContext.mRPK;
		LOG_DBG << "   mRuleFile = " << node->mInitialShapeContext.mRuleFile;
	}
	if (node->mInitialShapeContext.mAssetsMap == nullptr || node->mInitialShapeContext.mRPK.empty() || node->mInitialShapeContext.mRuleFile.empty()) {
		theMenu[0].setToken(0);
		return;
	}

	const wchar_t* cgbURI = node->mInitialShapeContext.mAssetsMap->getString(node->mInitialShapeContext.mRuleFile.c_str());
	if (cgbURI == nullptr) {
		LOG_ERR << L"failed to resolve rule file '" << node->mInitialShapeContext.mRuleFile << "', aborting.";
		return;
	}

	prt::Status status = prt::STATUS_UNSPECIFIED_ERROR;
	const prt::RuleFileInfo* rfi = prt::createRuleFileInfo(cgbURI, nullptr, &status);
	if (status == prt::STATUS_OK) {
		StringPairVector startRules, rules;
		for (size_t ri = 0; ri < rfi->getNumRules() ; ri++) {
			const prt::RuleFileInfo::Entry* re = rfi->getRule(ri);
			std::string rn = utils::toOSNarrowFromUTF16(re->getName());
			std::vector<std::string> tok;
			boost::split(tok, rn, boost::is_any_of("$"));

			bool hasStartRuleAnnotation = false;
			for (size_t ai = 0; ai < re->getNumAnnotations(); ai++) {
				if (std::wcscmp(re->getAnnotation(ai)->getName(), L"@StartRule") == 0) {
					hasStartRuleAnnotation = true;
					break;
				}
			}

			if (hasStartRuleAnnotation)
				startRules.emplace_back(tok[1], tok[1] + " (@StartRule)");
			else
				rules.emplace_back(tok[1], tok[1]);
		}

		std::sort(startRules.begin(), startRules.end(), compareSecond);
		std::sort(rules.begin(), rules.end(), compareSecond);
		rules.reserve(rules.size() + startRules.size());
		rules.insert(rules.begin(), startRules.begin(), startRules.end());

		const size_t limit = std::min<size_t>(rules.size(), theMaxSize);
		for (size_t ri = 0; ri < limit; ri++) {
			theMenu[ri].setToken(rules[ri].first.c_str());
			theMenu[ri].setLabel(rules[ri].second.c_str());
		}
		theMenu[limit].setToken(0); // need a null terminator
		rfi->destroy();
	}
}

void SOP_PRT::buildRuleFileMenu(void* data, PRM_Name* theMenu, int theMaxSize, const PRM_SpareData*, const PRM_Parm*) {
	SOP_PRT* node = static_cast<SOP_PRT*>(data);

	if (!node->mInitialShapeContext.mAssetsMap || node->mInitialShapeContext.mRPK.empty()) {
		theMenu[0].setToken(0);
		return;
	}

	std::vector<std::pair<std::wstring,std::wstring>> cgbs; // key -> uri
	getCGBs(node->mInitialShapeContext.mAssetsMap, cgbs);

	const size_t limit = std::min<size_t>(cgbs.size(), theMaxSize);
	for (size_t ri = 0; ri < limit; ri++) {
		std::string tok = utils::toOSNarrowFromUTF16(cgbs[ri].first);
		std::string lbl = utils::toOSNarrowFromUTF16(cgbs[ri].first); // TODO
		theMenu[ri].setToken(tok.c_str());
		theMenu[ri].setLabel(lbl.c_str());
	}
	theMenu[limit].setToken(0); // need a null terminator
}


} // namespace p4h