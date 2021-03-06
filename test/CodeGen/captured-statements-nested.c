// RUN: %clang_cc1 -fblocks -emit-llvm %s -o %t
// RUN: FileCheck %s -input-file=%t -check-prefix=CHECK1
// RUN: FileCheck %s -input-file=%t -check-prefix=CHECK2

struct A {
  long a;
  float b;
  char c;
};

void test_nest_captured_stmt(int param, long size, int param_arr[size]) {
  int w;
  int arr[param][size];
  // CHECK1: %struct.anon{{.*}} = type { i32*, i32*, i{{.+}}*, i32**, i32* }
  // CHECK1: %struct.anon{{.*}} = type { i32*, i32*, i32**, i32*, i{{.+}}*, i32**, i32* }
  // CHECK1: [[T:%struct.anon.*]] = type { i32*, i32*, %struct.A*, i32**, i32*, i{{.+}}*, i32**, i32* }
  #pragma clang __debug captured
  {
    int x;
    int *y = &w;
    #pragma clang __debug captured
    {
      struct A z;
      #pragma clang __debug captured
      {
        w = x = z.a = 1;
        *y = param;
        z.b = 0.1f;
        z.c = 'c';
        param_arr[size - 1] = 2;
        arr[10][z.a] = 12;

        // CHECK1: define internal void @__captured_stmt{{.*}}([[T]]
        //
        // CHECK1: getelementptr inbounds [[T]]* {{.*}}, i32 0, i32 2
        // CHECK1-NEXT: load %struct.A**
        // CHECK1-NEXT: getelementptr inbounds %struct.A*
        // CHECK1-NEXT: store i{{.+}} 1
        //
        // CHECK1: getelementptr inbounds [[T]]* {{.*}}, i32 0, i32 1
        // CHECK1-NEXT: load i32**
        // CHECK1-NEXT: store i32 1
        //
        // CHECK1: getelementptr inbounds [[T]]* {{.*}}, i32 0, i32 0
        // CHECK1-NEXT: load i32**
        // CHECK1-NEXT: store i32 1
        //
        // CHECK1: getelementptr inbounds [[T]]* {{.*}}, i32 0, i32 4
        // CHECK1-NEXT: load i32**
        // CHECK1-NEXT: load i32*
        // CHECK1-NEXT: getelementptr inbounds [[T]]* {{.*}}, i32 0, i32 3
        // CHECK1-NEXT: load i32***
        // CHECK1-NEXT: load i32**
        // CHECK1-NEXT: store i32
        //
        // CHECK1: getelementptr inbounds [[T]]* {{.*}}, i32 0, i32 2
        // CHECK1-NEXT: load %struct.A**
        // CHECK1-NEXT: getelementptr inbounds %struct.A*
        // CHECK1-NEXT: store float
        //
        // CHECK1: getelementptr inbounds [[T]]* {{.*}}, i32 0, i32 2
        // CHECK1-NEXT: load %struct.A**
        // CHECK1-NEXT: getelementptr inbounds %struct.A*
        // CHECK1-NEXT: store i8 99
        //
        // CHECK1: [[SIZE_ADDR_REF:%.*]] = getelementptr inbounds [[T]]* {{.*}}, i{{.+}} 0, i{{.+}} 5
        // CHECK1-DAG: [[SIZE_ADDR:%.*]] = load i{{.+}}** [[SIZE_ADDR_REF]]
        // CHECK1-DAG: [[SIZE:%.*]] = load i{{.+}}* [[SIZE_ADDR]]
        // CHECK1-DAG: [[PARAM_ARR_IDX:%.*]] = sub nsw i{{.+}} [[SIZE]], 1
        // CHECK1-DAG: [[PARAM_ARR_ADDR_REF:%.*]] = getelementptr inbounds [[T]]* {{.*}}, i{{.+}} 0, i{{.+}} 6
        // CHECK1-DAG: [[PARAM_ARR_ADDR:%.*]] = load i{{.+}}*** [[PARAM_ARR_ADDR_REF]]
        // CHECK1-DAG: [[PARAM_ARR:%.*]] = load i{{.+}}** [[PARAM_ARR_ADDR]]
        // CHECK1-DAG: [[PARAM_ARR_SIZE_MINUS_1_ADDR:%.*]] = getelementptr inbounds i{{.+}}* [[PARAM_ARR]], i{{.*}} [[PARAM_ARR_IDX]]
        // CHECK1: store i{{.+}} 2, i{{.+}}* [[PARAM_ARR_SIZE_MINUS_1_ADDR]]
        //
        // CHECK1: [[Z_ADDR_REF:%.*]] = getelementptr inbounds [[T]]* {{.*}}, i{{.+}} 0, i{{.+}} 2
        // CHECK1-DAG: [[Z_ADDR:%.*]] = load %struct.A** [[Z_ADDR_REF]]
        // CHECK1-DAG: [[Z_A_ADDR:%.*]] = getelementptr inbounds %struct.A* [[Z_ADDR]], i{{.+}} 0, i{{.+}} 0
        // CHECK1-DAG: [[ARR_IDX_2:%.*]] = load i{{.+}}* [[Z_A_ADDR]]
        // CHECK1-DAG: [[ARR_ADDR_REF:%.*]] = getelementptr inbounds [[T]]* {{.*}}, i{{.+}} 0, i{{.+}} 7
        // CHECK1-DAG: [[ARR_ADDR:%.*]] = load i{{.+}}** [[ARR_ADDR_REF]]
        // CHECK1-DAG: [[ARR_IDX_1:%.*]] = mul {{.*}} 10
        // CHECK1-DAG: [[ARR_10_ADDR:%.*]] = getelementptr inbounds i{{.+}}* [[ARR_ADDR]], i{{.*}} [[ARR_IDX_1]]
        // CHECK1-DAG: [[ARR_10_Z_A_ADDR:%.*]] = getelementptr inbounds i{{.+}}* [[ARR_10_ADDR]], i{{.*}} [[ARR_IDX_2]]
        // CHECK1: store i{{.+}} 12, i{{.+}}* [[ARR_10_Z_A_ADDR]]
      }
    }
  }
}

void test_nest_block() {
  __block int x;
  int y;
  ^{
    int z;
    x = z;
    #pragma clang __debug captured
    {
      z = y; // OK
    }
  }();

  // CHECK2: define internal void @{{.*}}test_nest_block_block_invoke
  //
  // CHECK2: [[Z:%[0-9a-z_]*]] = alloca i32
  // CHECK2: alloca %struct.anon{{.*}}
  //
  // CHECK2: store i32
  // CHECK2: store i32* [[Z]]
  //
  // CHECK2: getelementptr inbounds %struct.anon
  // CHECK2-NEXT: getelementptr inbounds
  // CHECK2-NEXT: store i32*
  //
  // CHECK2: call void @__captured_stmt

  int a;
  #pragma clang __debug captured
  {
    __block int b;
    int c;
    __block int d;
    ^{
      b = a;
      b = c;
      b = d;
    }();
  }

  // CHECK2: alloca %struct.__block_byref_b
  // CHECK2-NEXT: [[C:%[0-9a-z_]*]] = alloca i32
  // CHECK2-NEXT: alloca %struct.__block_byref_d
  //
  // CHECK2: bitcast %struct.__block_byref_b*
  // CHECK2-NEXT: store i8*
  //
  // CHECK2: [[CapA:%[0-9a-z_.]*]] = getelementptr inbounds {{.*}}, i32 0, i32 7
  //
  // CHECK2: getelementptr inbounds %struct.anon{{.*}}, i32 0, i32 0
  // CHECK2: load i32**
  // CHECK2: load i32*
  // CHECK2: store i32 {{.*}}, i32* [[CapA]]
  //
  // CHECK2: [[CapC:%[0-9a-z_.]*]] = getelementptr inbounds {{.*}}, i32 0, i32 8
  // CHECK2-NEXT: [[Val:%[0-9a-z_]*]] = load i32* [[C]]
  // CHECK2-NEXT: store i32 [[Val]], i32* [[CapC]]
  //
  // CHECK2: bitcast %struct.__block_byref_d*
  // CHECK2-NEXT: store i8*
}
