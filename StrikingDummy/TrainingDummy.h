#pragma once

#include "Job.h"
#include "Model.h"
#include "Rotation.h"

namespace StrikingDummy
{
	struct TrainingDummy
	{
		Job& job;
		Model model;
		ModelRotation rotation;

		TrainingDummy(Job& job);
		~TrainingDummy();

		void train();
		void test();
		void trace();
		void trace_mimu();
		void trace_sam();
		void trace_mch();
	};
}