#include "ir.h"
#include <numeric>
#include <Eigen/Dense>

TLANG_NAMESPACE_BEGIN

class IRPrinter : public IRVisitor {
 public:
  int current_indent;

  IRPrinter() {
    current_indent = -1;
  }

  template <typename... Args>
  void print(std::string f, Args &&... args) {
    for (int i = 0; i < current_indent; i++)
      fmt::print("  ");
    fmt::print(f, std::forward<Args>(args)...);
    fmt::print("\n");
  }

  static void run(IRNode *node) {
    auto p = IRPrinter();
    node->accept(&p);
  }

  void visit(StmtList *stmt_list) {
    current_indent++;
    for (auto &stmt : stmt_list->statements) {
      stmt->accept(this);
    }
    current_indent--;
  }

  void visit(AssignStmt *assign) {
    print("{} = {}", assign->id.name(), assign->rhs->serialize());
  }

  void visit(AllocaStmt *alloca) {
    print("{} alloca {}", alloca->type_hint(), alloca->lhs.name());
  }

  void visit(BinaryOpStmt *bin) {
    print("{} {} = {} {} {}", bin->type_hint(), bin->name(),
          binary_type_name(bin->type), bin->lhs->name(), bin->rhs->name());
  }

  void visit(IfStmt *if_stmt) {
    print("if {} {{", if_stmt->condition->serialize());
    if (if_stmt->true_statements)
      if_stmt->true_statements->accept(this);
    if (if_stmt->false_statements) {
      print("}} else {{");
      if_stmt->false_statements->accept(this);
    }
    print("}}");
  }

  void visit(FrontendPrintStmt *print_stmt) {
    print("print {}", print_stmt->expr.serialize());
  }

  void visit(PrintStmt *print_stmt) {
    print("{} print {}", print_stmt->type_hint(), print_stmt->stmt->name());
  }

  void visit(ConstStatement *const_stmt) {
    print("{} {} = const {}", const_stmt->type_hint(), const_stmt->name(),
          const_stmt->value);
  }

  void visit(ForStmt *for_stmt) {
    print("for {} in range({}, {}) {{", for_stmt->loop_var_id.name(),
          for_stmt->begin->serialize(), for_stmt->end->serialize());
    for_stmt->body->accept(this);
    print("}}");
  }

  void visit(LocalLoadStmt *stmt) {
    print("{} = load {}", stmt->name(), stmt->id.name());
  }

  void visit(LocalStoreStmt *stmt) {
    print("[store] {} = {}", stmt->id.name(), stmt->stmt->name());
  }
};

class IRModifiedException {};

// Lower Expr tree to a bunch of binary/unary(binary/unary) statements
// Goal: eliminate Expression, and mutable local variables. Make AST SSA.
class LowerAST : public IRVisitor {
 public:
  LowerAST() {
  }

  VecStatement expand(ExprH expr) {
    auto ret = VecStatement();
    expr->flatten(ret);
    return ret;
  }

  void visit(StmtList *stmt_list) {
    for (auto &stmt : stmt_list->statements) {
      stmt->accept(this);
    }
  }

  void visit(AllocaStmt *alloca) {
    // print("{} <- alloca {}", alloca->lhs.name(),
    // data_type_name(alloca->type));
  }

  void visit(BinaryOpStmt *bin) {  // this will not appear here
  }

  void visit(IfStmt *if_stmt) {
    // TODO
    if (if_stmt->true_statements)
      if_stmt->true_statements->accept(this);
    if (if_stmt->false_statements) {
      if_stmt->false_statements->accept(this);
    }
  }

  void visit(LocalLoadStmt *) {
  }

  void visit(LocalStoreStmt *) {
  }

  void visit(PrintStmt *stmt) {
  }

  void visit(FrontendPrintStmt *stmt) {
    // expand rhs
    auto expr = stmt->expr;
    VecStatement flattened;
    expr->flatten(flattened);
    auto print = std::make_unique<PrintStmt>(flattened.back().get());
    flattened.push_back(std::move(print));
    stmt->parent->replace_with(stmt, flattened);
    throw IRModifiedException();
  }

  void visit(ConstStatement *const_stmt) {  // this will not appear here
  }

  void visit(ForStmt *for_stmt) {
    for_stmt->body->accept(this);
  }

  void visit(AssignStmt *assign) {
    // expand rhs
    auto expr = assign->rhs;
    VecStatement flattened;
    expr->flatten(flattened);
    if (true) {  // local variable
      // emit local store stmt
      auto local_store =
          std::make_unique<LocalStoreStmt>(assign->id, flattened.back().get());
      flattened.push_back(std::move(local_store));
    } else {  // global variable
    }
    assign->parent->replace_with(assign, flattened);
    throw IRModifiedException();
  }

  static void run(IRNode *node) {
    LowerAST inst;
    while (true) {
      bool modified = false;
      try {
        node->accept(&inst);
      } catch (IRModifiedException) {
        modified = true;
      }
      if (!modified)
        break;
    }
  }
};

// Vector width, vectorization plan etc
class PropagateSchedule : public IRVisitor {};

// "Type" here does not include vector width
class TypeCheck : public IRVisitor {
  TypeCheck() {
    allow_undefined_visitor = true;
  }
};

#define declare(x) \
  auto x = ExpressionHandle(std::make_shared<IdExpression>(#x));

#define var(type, x) Var<type>(x);

auto test_ast = []() {
  CoreState::set_trigger_gdb_when_crash(true);
  declare(a);
  declare(b);
  declare(p);
  declare(q);
  declare(i);
  declare(j);

  var(float32, a);
  var(float32, b);

  var(int32, p);
  var(int32, q);

  a = a + b;
  p = p + q;

  Print(a);
  If(a < 500).Then([&] { Print(b); }).Else([&] { Print(a); });

  If(a > 5)
      .Then([&] {
        b = (b + 1) / 3;
        b = b * 3;
      })
      .Else([&] {
        b = b + 2;
        b = b - 4;
      });

  For(i, 0, 100, [&] {
    For(j, 0, 200, [&] {
      ExprH k = i + j;
      Print(k);
      // While(k < 500, [&] { Print(k); });
    });
  });
  Print(b);

  LowerAST::run(context.root());
  IRPrinter::run(context.root());

};
TC_REGISTER_TASK(test_ast);

TLANG_NAMESPACE_END
