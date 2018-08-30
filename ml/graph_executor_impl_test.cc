// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <base/run_loop.h>
#include <base/callback_helpers.h>
#include <base/command_line.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/flag_helper.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/test_helpers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tensorflow/contrib/lite/context.h>
#include <tensorflow/contrib/lite/interpreter.h>
#include <mojo/edk/embedder/embedder.h>
#include <mojo/edk/embedder/process_delegate.h>

#include "ml/graph_executor_impl.h"
#include "ml/tensor_view.h"
#include "mojom/graph_executor.mojom.h"
#include "mojom/tensor.mojom.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::GraphExecutor;
using ::chromeos::machine_learning::mojom::GraphExecutorPtr;
using ::chromeos::machine_learning::mojom::Int64List;
using ::chromeos::machine_learning::mojom::StringList;
using ::chromeos::machine_learning::mojom::Tensor;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::chromeos::machine_learning::mojom::ValueList;
using ::testing::ElementsAre;

// Create a tensor with the given shape and values. Does no validity checking
// (by design, as we sometimes need to pass bad tensors to test error handling).
template <typename T>
TensorPtr NewTensor(const std::vector<int64_t>& shape,
                    const std::vector<T>& values) {
  TensorPtr tensor(Tensor::New());
  TensorView<T> tensor_view(tensor);
  tensor_view.Allocate();

  mojo::Array<int64_t>& tensor_shape = tensor_view.GetShape();
  for (const int64_t dim : shape) {
    tensor_shape.push_back(dim);
  }

  mojo::Array<T>& tensor_values = tensor_view.GetValues();
  for (const T& value : values) {
    tensor_values.push_back(value);
  }

  return tensor;
}

// Make the simplest possible model: one that copies its input to its output.
std::unique_ptr<tflite::Interpreter> IdentityInterpreter() {
  auto interpreter = std::make_unique<tflite::Interpreter>();

  // Add the input and output tensors.
  CHECK_EQ(interpreter->AddTensors(2), kTfLiteOk);
  CHECK_EQ(interpreter->SetInputs({0}), kTfLiteOk);
  CHECK_EQ(interpreter->SetOutputs({1}), kTfLiteOk);

  // Set the types of the tensors.
  TfLiteQuantizationParams quantized;
  CHECK_EQ(interpreter->SetTensorParametersReadWrite(
               0 /*tensor_index*/, kTfLiteFloat32 /*type*/,
               "in_tensor" /*name*/, {1} /*dims*/, quantized),
           kTfLiteOk);
  CHECK_EQ(interpreter->SetTensorParametersReadWrite(
               1 /*tensor_index*/, kTfLiteFloat32 /*type*/,
               "out_tensor" /*name*/, {1} /*dims*/, quantized),
           kTfLiteOk);

  // Add the copy node.
  TfLiteRegistration reg_copy = {nullptr, nullptr, nullptr, nullptr};
  reg_copy.invoke = [](TfLiteContext* const context, TfLiteNode* const node) {
    TfLiteTensor* const input = &context->tensors[node->inputs->data[0]];
    TfLiteTensor* const output = &context->tensors[node->outputs->data[0]];
    *output->data.f = *input->data.f;
    return kTfLiteOk;
  };

  CHECK_EQ(interpreter->AddNodeWithParameters(
               {0} /*inputs*/, {1} /*outputs*/, nullptr /*init_data*/,
               0 /*init_data_size*/, nullptr /*builtin_data*/, &reg_copy),
           kTfLiteOk);

  CHECK_EQ(interpreter->AllocateTensors(), kTfLiteOk);

  return interpreter;
}

// Test two normal executions of a graph.
TEST(GraphExecutorTest, TestOk) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Execute once.
  {
    mojo::Map<mojo::String, TensorPtr> inputs;
    inputs.insert("in_tensor", NewTensor<double>({1}, {0.5}));
    mojo::Array<mojo::String> outputs({"out_tensor"});

    bool callback_done = false;
    graph_executor->Execute(
        std::move(inputs), std::move(outputs),
        [&callback_done](const ExecuteResult result,
                         const mojo::Array<TensorPtr> outputs) {
          // Check that the inference succeeded and gives the expected number of
          // outputs.
          EXPECT_EQ(result, ExecuteResult::OK);
          ASSERT_EQ(outputs.size(), 1);

          // Check that the output tensor has the right type and format.
          const TensorView<double> out_tensor(outputs[0]);
          EXPECT_TRUE(out_tensor.IsValidType());
          EXPECT_TRUE(out_tensor.IsValidFormat());

          // Check the output tensor has the expected shape and values.
          EXPECT_THAT(out_tensor.GetShape().storage(), ElementsAre(1));
          EXPECT_THAT(out_tensor.GetValues().storage(), ElementsAre(0.5));

          callback_done = true;
        });

    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(callback_done);
  }

  // Execute again with different input.
  {
    mojo::Map<mojo::String, TensorPtr> inputs;
    inputs.insert("in_tensor", NewTensor<double>({1}, {0.75}));
    mojo::Array<mojo::String> outputs({"out_tensor"});

    bool callback_done = false;
    graph_executor->Execute(
        std::move(inputs), std::move(outputs),
        [&callback_done](const ExecuteResult result,
                         const mojo::Array<TensorPtr> outputs) {
          // Check that the inference succeeded and gives the expected number of
          // outputs.
          EXPECT_EQ(result, ExecuteResult::OK);
          ASSERT_EQ(outputs.size(), 1);

          // Check that the output tensor has the right type and format.
          const TensorView<double> out_tensor(outputs[0]);
          EXPECT_TRUE(out_tensor.IsValidType());
          EXPECT_TRUE(out_tensor.IsValidFormat());

          // Check the output tensor has the expected shape and values.
          EXPECT_THAT(out_tensor.GetShape().storage(), ElementsAre(1));
          EXPECT_THAT(out_tensor.GetValues().storage(), ElementsAre(0.75));

          callback_done = true;
        });

    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(callback_done);
  }
}

// Test execution of a graph that requires int narrowing from input tensors.
TEST(GraphExecutorTest, TestNarrowing) {
  // Create a model that copies a bool into a uint8.
  auto interpreter = std::make_unique<tflite::Interpreter>();

  // Add the input and output tensors.
  CHECK_EQ(interpreter->AddTensors(2), kTfLiteOk);
  CHECK_EQ(interpreter->SetInputs({0}), kTfLiteOk);
  CHECK_EQ(interpreter->SetOutputs({1}), kTfLiteOk);

  // Set the types of the tensors.
  TfLiteQuantizationParams quantized;
  CHECK_EQ(interpreter->SetTensorParametersReadWrite(
               0 /*tensor_index*/, kTfLiteBool /*type*/, "in_tensor" /*name*/,
               {1} /*dims*/, quantized),
           kTfLiteOk);
  CHECK_EQ(interpreter->SetTensorParametersReadWrite(
               1 /*tensor_index*/, kTfLiteUInt8 /*type*/, "out_tensor" /*name*/,
               {1} /*dims*/, quantized),
           kTfLiteOk);

  // Add the copy node.
  TfLiteRegistration reg_copy = {nullptr, nullptr, nullptr, nullptr};
  reg_copy.invoke = [](TfLiteContext* const context, TfLiteNode* const node) {
    TfLiteTensor* const input = &context->tensors[node->inputs->data[0]];
    TfLiteTensor* const output = &context->tensors[node->outputs->data[0]];
    *output->data.uint8 = *input->data.b;
    return kTfLiteOk;
  };

  CHECK_EQ(interpreter->AddNodeWithParameters(
               {0} /*inputs*/, {1} /*outputs*/, nullptr /*init_data*/,
               0 /*init_data_size*/, nullptr /*builtin_data*/, &reg_copy),
           kTfLiteOk);

  CHECK_EQ(interpreter->AllocateTensors(), kTfLiteOk);

  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              std::move(interpreter),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // We represent bools with int64 tensors.
  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<int64_t>({1}, {true}));
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        // Check that the inference succeeded and gives the expected number of
        // outputs.
        EXPECT_EQ(result, ExecuteResult::OK);
        ASSERT_EQ(outputs.size(), 1);

        // Check that the output tensor has the right type and format.
        const TensorView<int64_t> out_tensor(outputs[0]);
        EXPECT_TRUE(out_tensor.IsValidType());
        EXPECT_TRUE(out_tensor.IsValidFormat());

        // Check the output tensor has the expected shape and values.
        EXPECT_THAT(out_tensor.GetShape().storage(), ElementsAre(1));
        EXPECT_THAT(out_tensor.GetValues().storage(), ElementsAre(1));

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the client requests a bad output tensor name.
TEST(GraphExecutorTest, TestInvalidOutputName) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<double>({1}, {0.5}));
  // Ask for the input tensor (which isn't in our "outputs" list).
  mojo::Array<mojo::String> outputs({"in_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::UNKNOWN_OUTPUT_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the client does not request an output.
TEST(GraphExecutorTest, TestMissingOutputName) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<double>({1}, {0.5}));

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), mojo::Array<mojo::String>(),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::OUTPUT_MISSING_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the client requests the same output name twice.
TEST(GraphExecutorTest, TestDuplicateOutputName) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<double>({1}, {0.5}));
  // Ask for two copies of the output tensor.
  mojo::Array<mojo::String> outputs({"out_tensor", "out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::DUPLICATE_OUTPUT_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the client supplies a bad input tensor name.
TEST(GraphExecutorTest, TestInvalidInputName) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Specify a value for the output tensor (which isn't in our "inputs" list).
  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("out_tensor", NewTensor<double>({1}, {0.5}));
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::UNKNOWN_INPUT_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the client does not supply an input.
TEST(GraphExecutorTest, TestMissingInputName) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Specify a value for the output tensor (which isn't in our "inputs" list).
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      mojo::Map<mojo::String, TensorPtr>(), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::INPUT_MISSING_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the client supplies input of incorrect type.
TEST(GraphExecutorTest, TestWrongInputType) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Give an int tensor when a float tensor is expected.
  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<int64_t>({1}, {123}));
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::INPUT_TYPE_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the client supplies input of incorrect shape.
TEST(GraphExecutorTest, TestWrongInputShape) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Give a 1x1 tensor when a scalar is expected.
  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<double>({1, 1}, {0.5}));
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::INPUT_SHAPE_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the user supplies input in a bad format (i.e. with
// mismatched shape and values).
TEST(GraphExecutorTest, TestInvalidInputFormat) {
  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              IdentityInterpreter(),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Give a tensor with scalar shape but multiple values.
  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<double>({1}, {0.5, 0.5}));
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::INPUT_FORMAT_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the graph accepts tensors of an unsupported type.
TEST(GraphExecutorTest, TestInvalidInputNodeType) {
  // Define an interpreter that accepts strings (which are not yet supported).
  auto interpreter = std::make_unique<tflite::Interpreter>();

  // Add the input and output tensors.
  ASSERT_EQ(interpreter->AddTensors(2), kTfLiteOk);
  ASSERT_EQ(interpreter->SetInputs({0}), kTfLiteOk);
  ASSERT_EQ(interpreter->SetOutputs({1}), kTfLiteOk);

  // Set the type of the input tensor to string.
  TfLiteQuantizationParams quantized;
  ASSERT_EQ(interpreter->SetTensorParametersReadWrite(
                0 /*tensor_index*/, kTfLiteString /*type*/,
                "in_tensor" /*name*/, {1} /*dims*/, quantized),
            kTfLiteOk);
  ASSERT_EQ(interpreter->SetTensorParametersReadWrite(
                1 /*tensor_index*/, kTfLiteFloat32 /*type*/,
                "out_tensor" /*name*/, {1} /*dims*/, quantized),
            kTfLiteOk);

  ASSERT_EQ(interpreter->AllocateTensors(), kTfLiteOk);

  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              std::move(interpreter),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Graph execution should fail before we get to input type checking.
  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<double>({}, {}));
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::EXECUTION_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where TF lite invocation fails.
TEST(GraphExecutorTest, TestExecutionFailure) {
  // Use an uninitialized interpreter, which induces an execution failure.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> inputs_outputs;
  const GraphExecutorImpl graph_executor_impl(
      inputs_outputs, inputs_outputs, std::make_unique<tflite::Interpreter>(),
      mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  bool callback_done = false;
  graph_executor->Execute(
      mojo::Map<mojo::String, TensorPtr>(), mojo::Array<mojo::String>(),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::EXECUTION_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the graph produces tensors of an unsupported type.
TEST(GraphExecutorTest, TestInvalidOutputNodeType) {
  // Define an interpreter that produces strings (which are not yet supported).
  auto interpreter = std::make_unique<tflite::Interpreter>();

  // Add the input and output tensors.
  ASSERT_EQ(interpreter->AddTensors(2), kTfLiteOk);
  ASSERT_EQ(interpreter->SetInputs({0}), kTfLiteOk);
  ASSERT_EQ(interpreter->SetOutputs({1}), kTfLiteOk);

  // Set the type of the output tensor to string.
  TfLiteQuantizationParams quantized;
  ASSERT_EQ(interpreter->SetTensorParametersReadWrite(
                0 /*tensor_index*/, kTfLiteFloat32 /*type*/,
                "in_tensor" /*name*/, {1} /*dims*/, quantized),
            kTfLiteOk);
  ASSERT_EQ(interpreter->SetTensorParametersReadWrite(
                1 /*tensor_index*/, kTfLiteString /*type*/,
                "out_tensor" /*name*/, {1} /*dims*/, quantized),
            kTfLiteOk);

  ASSERT_EQ(interpreter->AllocateTensors(), kTfLiteOk);

  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              std::move(interpreter),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Populate input.
  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<double>({1}, {0.5}));
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::EXECUTION_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

// Test graph execution where the graph produces output of invalid shape.
TEST(GraphExecutorTest, TestInvalidOutputNodeShape) {
  // Define an interpreter that produces an empty output (i.e. simulates an
  // internal model error).
  auto interpreter = std::make_unique<tflite::Interpreter>();

  // Add the input and output tensors.
  ASSERT_EQ(interpreter->AddTensors(2), kTfLiteOk);
  ASSERT_EQ(interpreter->SetInputs({0}), kTfLiteOk);
  ASSERT_EQ(interpreter->SetOutputs({1}), kTfLiteOk);

  // Set the shape of the output tensor to be empty.
  TfLiteQuantizationParams quantized;
  ASSERT_EQ(interpreter->SetTensorParametersReadWrite(
                0 /*tensor_index*/, kTfLiteFloat32 /*type*/,
                "in_tensor" /*name*/, {1} /*dims*/, quantized),
            kTfLiteOk);
  ASSERT_EQ(interpreter->SetTensorParametersReadWrite(
                1 /*tensor_index*/, kTfLiteFloat32 /*type*/,
                "out_tensor" /*name*/, {} /*dims*/, quantized),
            kTfLiteOk);

  ASSERT_EQ(interpreter->AllocateTensors(), kTfLiteOk);

  // Create GraphExecutor.
  GraphExecutorPtr graph_executor;
  const std::map<std::string, int> input_names = {{"in_tensor", 0}};
  const std::map<std::string, int> output_names = {{"out_tensor", 1}};
  const GraphExecutorImpl graph_executor_impl(input_names, output_names,
                                              std::move(interpreter),
                                              mojo::GetProxy(&graph_executor));
  ASSERT_TRUE(graph_executor.is_bound());

  // Populate input.
  mojo::Map<mojo::String, TensorPtr> inputs;
  inputs.insert("in_tensor", NewTensor<double>({1}, {0.5}));
  mojo::Array<mojo::String> outputs({"out_tensor"});

  bool callback_done = false;
  graph_executor->Execute(
      std::move(inputs), std::move(outputs),
      [&callback_done](const ExecuteResult result,
                       const mojo::Array<TensorPtr> outputs) {
        EXPECT_EQ(result, ExecuteResult::EXECUTION_ERROR);
        EXPECT_TRUE(outputs.is_null());

        callback_done = true;
      });

  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_done);
}

}  // namespace
}  // namespace ml

// A ProcessDelegate that does nothing upon IPC system shutdown.
class DoNothingProcessDelegate : public mojo::edk::ProcessDelegate {
 public:
  void OnShutdownComplete() override {}
};

int main(int argc, char** argv) {
  SetUpTests(&argc, argv, true);

  (new brillo::BaseMessageLoop())->SetAsCurrent();

  mojo::edk::Init();
  mojo::edk::InitIPCSupport(new DoNothingProcessDelegate(),
                            base::ThreadTaskRunnerHandle::Get());

  return RUN_ALL_TESTS();
}