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
		void metrics();
		void dist(int seconds, int times);
		void study();
	};
}