#include "Ast.h"
#include "SymbolTable.h"
#include "Unit.h"
#include "Instruction.h"
#include "IRBuilder.h"
#include <string>
#include "Type.h"

extern FILE *yyout;
int Node::counter = 0;
IRBuilder* Node::builder = nullptr;

Node::Node()
{
    seq = counter++;
}

void Node::backPatch(std::vector<BasicBlock**> &list, BasicBlock*target)
{
    for(auto &bb:list)
        *bb = target;
}

std::vector<BasicBlock**> Node::merge(std::vector<BasicBlock**> &list1, std::vector<BasicBlock**> &list2)
{
    std::vector<BasicBlock**> res(list1);
    res.insert(res.end(), list2.begin(), list2.end());
    return res;
}

void Ast::genCode(Unit *unit)
{
    IRBuilder *builder = new IRBuilder(unit);
    Node::setIRBuilder(builder);
    if(root)
        root->genCode();
}

void FunctionDef::genCode()
{
    Unit *unit = builder->getUnit();
    std::vector<SymbolEntry*> param_syms;
    if(params)
        params->getSyms(param_syms);
    Function *func = new Function(unit, se, param_syms);
    BasicBlock *entry = func->getEntry();
    // set the insert point to the entry basicblock of this function.
    builder->setInsertBB(entry);

    if(params)
        params->genCode();
    stmt->genCode();

    for (auto &bb:func->getBlockList())
    {
        Instruction *inst = bb->rbegin();
        if(inst->isCond())
        {
            CondBrInstruction *br;
            BasicBlock *succ1, *succ2;
            br = dynamic_cast<CondBrInstruction*>(inst);
            succ1 = br->getTrueBranch();
            succ2 = br->getFalseBranch();
            bb->addSucc(succ1);
            bb->addSucc(succ2);
            succ1->addPred(bb);
            succ2->addPred(bb);
        }
        else if(inst->isUncond())
        {
            UncondBrInstruction *br;
            BasicBlock *succ;
            br = dynamic_cast<UncondBrInstruction*>(inst);
            succ = br->getBranch();

            bb->addSucc(succ);
            succ->addPred(bb);
        }
    }
    // if(func -> getBlockList()[(func -> getBlockList()).size() - 1] -> empty())
    // {
    new RetInstruction(nullptr, func -> getBlockList()[(func -> getBlockList()).size() - 1]);
    // }
}

void BinaryExpr::genCode()
{
    BasicBlock *bb = builder->getInsertBB();
    Function *func = bb->getParent();
    if (op == AND)
    {
        BasicBlock *trueBB = new BasicBlock(func);  // if the result of lhs is true, jump to the trueBB.
        builder->setGenBr(true);
        expr1->genCode();
        backPatch(expr1->trueList(), trueBB);
        builder->setInsertBB(trueBB);               // set the insert point to the trueBB so that intructions generated by expr2 will be inserted into it.
        builder->setGenBr(true);
        expr2->genCode();
        true_list = expr2->trueList();
        false_list = merge(expr1->falseList(), expr2->falseList());
    }
    else if(op == OR)
    {
        BasicBlock *falseBB = new BasicBlock(func);
        builder->setGenBr(true);
        expr1->genCode();
        backPatch(expr1->falseList(), falseBB);
        builder->setInsertBB(falseBB);
        builder->setGenBr(true);
        expr2->genCode();
        false_list = expr2->falseList();
        true_list = merge(expr1->trueList(), expr2->trueList());
    }
    else if(op >= L && op <= NE)
    {
        bool gen_br = builder->genBr();
        builder->setGenBr(false);
        expr1->genCode();
        builder->setGenBr(false);
        expr2->genCode();
        Operand *src1 = expr1->getOperand();
        Operand *src2 = expr2->getOperand();
        int opcode;
        switch (op)
        {
        case L:
            opcode = CmpInstruction::L;
            break;
        case G:
            opcode = CmpInstruction::G;
            break;
        case LE:
            opcode = CmpInstruction::LE;
            break;
        case GE:
            opcode = CmpInstruction::GE;
            break;
        case NE:
            opcode = CmpInstruction::NE;
            break;
        case EQ:
            opcode = CmpInstruction::EQ;
            break; 
        }
        new CmpInstruction(opcode, dst, src1, src2, bb);
        if(gen_br)
        {
            CondBrInstruction *cond;
            cond = new CondBrInstruction(nullptr, nullptr, dst, bb);    // the target basicblock is unknown.
            true_list.push_back(cond->truePatchBranch());
            false_list.push_back(cond->falsePatchBranch());
        }
    }
    else if(op >= ADD && op <= MOD)
    {
        builder->setGenBr(false);
        expr1->genCode();
        builder->setGenBr(false);
        expr2->genCode();
        Operand *src1 = expr1->getOperand();
        Operand *src2 = expr2->getOperand();
        int opcode;
        switch (op)
        {
        case ADD:
            opcode = BinaryInstruction::ADD;
            break;
        case SUB:
            opcode = BinaryInstruction::SUB;
            break;
        case MUL:
            opcode = BinaryInstruction::MUL;
            break;
        case DIV:
            opcode = BinaryInstruction::DIV;
            break;
        case MOD:
            opcode = BinaryInstruction::MOD;
        }
        new BinaryInstruction(opcode, dst, src1, src2, bb);
    }
}

bool BinaryExpr::constantFolding(int &val) 
{
    int val1, val2;
    if(!expr1->constantFolding(val1))
        return false;
    if(!expr2->constantFolding(val2))
        return false;
    switch (op)
    {
        case ADD:
            val = val1 + val2;
            break;
        case SUB:
            val = val1 - val2;
            break;
        case MUL:
            val = val1 * val2;
            break;
        case DIV:
            val = val1 / val2;
            break;
        case MOD:
            val = val1 % val2;
    }
    return true;
}

void UnaryExpr::typeCheck() 
{
    expr->typeCheck();
    Type *type = expr->getSymPtr()->getType();
    if(op == NOT)
    {
        if(!type->isI1())
        {
            SymbolEntry *se = new ConstantSymbolEntry(type, 0);
            Constant *zero = new Constant(se);
            expr = new BinaryExpr(new TemporarySymbolEntry(IntType::get(1), SymbolTable::getLabel()), BinaryExpr::NE, expr, zero);
            type = IntType::get(1);
        }
    }
    else if(op == UMINUS)
    {
        if(!type->isI32())
        {
            expr = new Extend(new TemporarySymbolEntry(IntType::get(32), SymbolTable::getLabel()), expr);
            type = IntType::get(32);
        }
    }
    symbolEntry->setType(type);
}

void UnaryExpr::genCode() 
{
    bool gen_br = builder->genBr();
    expr->genCode();
    if(op == UMINUS)
    {
        if(!gen_br)
        {
            BasicBlock *bb = builder->getInsertBB();
            SymbolEntry *se = new ConstantSymbolEntry(expr->getSymPtr()->getType(), 0);
            Operand *zero = new Operand(se);
            new BinaryInstruction(BinaryInstruction::SUB, dst, zero, expr->getOperand(), bb);
        }
        else
        {
            true_list = expr->trueList();
            false_list = expr->falseList();
        }
    }
    else if(op == NOT)
    {
        if(gen_br)
        {
            true_list = expr->falseList();
            false_list = expr->trueList();
        }
        else
        {
            BasicBlock *bb = builder->getInsertBB();
            SymbolEntry *se = new ConstantSymbolEntry(IntType::get(1), 1);
            Operand *one = new Operand(se);
            new BinaryInstruction(BinaryInstruction::XOR, dst, expr->getOperand(), one, bb);
        }
    }
}

bool UnaryExpr::constantFolding(int &val) 
{
    if(!expr->constantFolding(val))
        return false;
    switch (op)
    {
        case UMINUS:
            val = -val;
            break;
        case NOT:
            val = !val;
            break;
    }
    return true;
}

void FuncRParams::typeCheck() 
{
    params->typeCheck();
    param->typeCheck();
}

void FuncRParams::genCode() 
{
    params->genCode();
    param->genCode();
}

void FuncRParams::getOperands(std::vector<Operand*> &operands) 
{
    params->getOperands(operands);
    param->getOperands(operands);
}

void Constant::genCode()
{
    // we don't need to generate code.
}

bool Constant::constantFolding(int &val) 
{
    val = dynamic_cast<ConstantSymbolEntry*>(symbolEntry)->getValue();
    return true;
}

void Id::genCode()
{
    BasicBlock *bb = builder->getInsertBB();
    Operand *addr = dynamic_cast<IdentifierSymbolEntry*>(symbolEntry)->getAddr();
    new LoadInstruction(dst, addr, bb);
}

bool Id::constantFolding(int &val) 
{
    IdentifierSymbolEntry *se = dynamic_cast<IdentifierSymbolEntry*>(symbolEntry);
    val = se->getInitVal();
    return se->isGlobal(); // conservative
}

void CallExpr::typeCheck() 
{
    Type *retType = dynamic_cast<FunctionType*>(callee->getType())->getRetType();
    if(retType->isVoid())
    {
        delete dst;
        dst = nullptr;
    }
}

void CallExpr::genCode() 
{
    if(params)
        params->genCode();
    std::vector<Operand*> p;
    if(params != nullptr)
        params->getOperands(p);
    BasicBlock *bb = builder->getInsertBB();
    new CallInstruction(dst, p, callee, bb);
}

void IfStmt::genCode()
{
    Function *func;
    BasicBlock *then_bb, *end_bb;

    func = builder->getInsertBB()->getParent();
    then_bb = new BasicBlock(func);
    end_bb = new BasicBlock(func);

    then_bb -> addPred(builder->getInsertBB());//设置其前驱
    builder -> getInsertBB() -> addSucc(then_bb);//设置后继
    end_bb -> addPred(then_bb);
    then_bb -> addSucc(end_bb);//
    end_bb -> addPred(builder -> getInsertBB());
    builder -> getInsertBB() -> addSucc(end_bb);


    builder->setGenBr(true);
    cond->genCode();
    builder->setGenBr(false);
    backPatch(cond->trueList(), then_bb);
    backPatch(cond->falseList(), end_bb);

    builder->setInsertBB(then_bb);
    thenStmt->genCode();
    then_bb = builder->getInsertBB();
    new UncondBrInstruction(end_bb, then_bb);

    builder->setInsertBB(end_bb);
}

void IfElseStmt::genCode()
{
    Function *func;
    BasicBlock *then_bb, *else_bb, *end_bb;

    func = builder->getInsertBB()->getParent();
    then_bb = new BasicBlock(func);
    else_bb = new BasicBlock(func);
    end_bb = new BasicBlock(func);

    // then_bb -> addPred(builder -> getInsertBB());
    // builder -> getInsertBB() -> addSucc(then_bb);

    // else_bb -> addPred(builder -> getInsertBB());
    // builder -> getInsertBB() -> addSucc(else_bb);

    // end_bb -> addPred(then_bb);
    // then_bb -> addSucc(end_bb);
    // end_bb -> addPred(else_bb);
    // else_bb -> addSucc(end_bb);


    builder->setGenBr(true);
    cond->genCode();
    builder->setGenBr(false);
    backPatch(cond->trueList(), then_bb);
    backPatch(cond->falseList(), else_bb);

    builder->setInsertBB(then_bb);
    thenStmt->genCode();
    then_bb = builder->getInsertBB();
    new UncondBrInstruction(end_bb, then_bb);

    builder->setInsertBB(else_bb);
    elseStmt->genCode();
    else_bb = builder->getInsertBB();
    new UncondBrInstruction(end_bb, else_bb);
    builder->setInsertBB(end_bb);
}

void WhileStmt::typeCheck() 
{
    cond->typeCheck();
    body->typeCheck();
    Type *type = cond->getSymPtr()->getType();
    if(!type->isI1())
    {
        SymbolEntry *se = new ConstantSymbolEntry(type, 0);
        Constant *zero = new Constant(se);
        cond = new BinaryExpr(new TemporarySymbolEntry(IntType::get(1), SymbolTable::getLabel()), BinaryExpr::NE, cond, zero);
    }
}

void WhileStmt::genCode() 
{
    Function *func;
    BasicBlock *cond_bb, *body_bb, *end_bb, *insert_bb;

    func = builder->getInsertBB()->getParent();
    cond_bb = new BasicBlock(func);
    body_bb = new BasicBlock(func);
    end_bb = new BasicBlock(func);

    cond_bb -> addPred(builder -> getInsertBB());
    builder -> getInsertBB() -> addSucc(cond_bb);
    body_bb -> addPred(cond_bb);
    cond_bb -> addSucc(body_bb);

    //builder -> getInsertBB() -> addSucc(loop_bb);
    end_bb -> addPred(body_bb);
    body_bb -> addSucc(end_bb);

    end_bb -> addPred(cond_bb);
    cond_bb -> addSucc(end_bb);



    insert_bb = builder->getInsertBB();
    new UncondBrInstruction(cond_bb, insert_bb);

    builder->setInsertBB(cond_bb);
    builder->setGenBr(true);
    cond->genCode();
    builder->setGenBr(false);
    backPatch(cond->trueList(), body_bb);
    backPatch(cond->falseList(), end_bb);

    builder->setInsertBB(body_bb);
    body->genCode();
    insert_bb = builder->getInsertBB();
    new UncondBrInstruction(cond_bb, insert_bb);

    builder->setInsertBB(end_bb);   
}

void CompoundStmt::genCode()
{
    stmt->genCode();
}

void SeqNode::genCode()
{
    stmt1->genCode();
    stmt2->genCode();
}

void Extend::genCode() 
{
    expr->genCode();
    BasicBlock *bb = builder->getInsertBB();
    new ExtInstruction(dst, expr->getOperand(), bb);
}

void DeclStmt::genCode()
{
    IdentifierSymbolEntry *se = dynamic_cast<IdentifierSymbolEntry *>(id->getSymPtr());
    if(se->isGlobal())
    {
        Operand *addr;
        SymbolEntry *addr_se;
        addr_se = new IdentifierSymbolEntry(*se);
        addr_se->setType(PointerType::get(se->getType()));
        addr = new Operand(addr_se);
        se->setAddr(addr);
    }
    else if(se->isLocal())
    {
        Function *func = builder->getInsertBB()->getParent();
        BasicBlock *entry = func->getEntry();
        Instruction *alloca;
        Operand *addr;
        SymbolEntry *addr_se;
        Type *type;
        type = PointerType::get(se->getType());
        addr_se = new TemporarySymbolEntry(type, SymbolTable::getLabel());
        addr = new Operand(addr_se);
        alloca = new AllocaInstruction(addr, se);                   // allocate space for local id in function stack.
        entry->insertFront(alloca);                                 // allocate instructions should be inserted into the begin of the entry block.
        se->setAddr(addr);                                          // set the addr operand in symbol entry so that we can use it in subsequent code generation.
        if(initVal != nullptr)
        {
            BasicBlock *bb = builder->getInsertBB();
            initVal->genCode();
            Operand *src = initVal->getOperand();
            new StoreInstruction(addr, src, bb);
        }
    }
}

void ConstDecl::typeCheck() 
{
    IdentifierSymbolEntry *se = dynamic_cast<IdentifierSymbolEntry *>(id->getSymPtr());
    if(se->isGlobal())
    {
        int my_val;
        if(initVal == nullptr)
            exit(-1);
        if(!initVal->constantFolding(my_val))
            exit(-1);
        se->setInitVal(my_val);
    }
}

void ConstDecl::genCode() 
{
    IdentifierSymbolEntry *se = dynamic_cast<IdentifierSymbolEntry *>(id->getSymPtr());
    if(se->isGlobal())
    {
        Operand *addr;
        SymbolEntry *addr_se;
        addr_se = new IdentifierSymbolEntry(*se);
        addr_se->setType(PointerType::get(se->getType()));
        addr = new Operand(addr_se);
        se->setAddr(addr);
    }
    else if(se->isLocal())
    {
        Function *func = builder->getInsertBB()->getParent();
        BasicBlock *entry = func->getEntry();
        BasicBlock *bb = builder->getInsertBB();
        Instruction *alloca;
        Operand *addr;
        SymbolEntry *addr_se;
        Type *type;
        type = PointerType::get(se->getType());
        addr_se = new TemporarySymbolEntry(type, SymbolTable::getLabel());
        addr = new Operand(addr_se);
        alloca = new AllocaInstruction(addr, se);
        entry->insertFront(alloca);
        se->setAddr(addr);
        
        initVal->genCode();
        Operand *src = initVal->getOperand();
        new StoreInstruction(addr, src, bb);
    }
}

void DefList::genCode() 
{
    def1->genCode();
    def2->genCode();
}

void FuncFParams::genCode() 
{
    params->genCode();
    param->genCode();
}

void FuncFParams::getTypes(std::vector<Type*>&types) 
{
    params->getTypes(types);
    param->getTypes(types);
}

void FuncFParams::getSyms(std::vector<SymbolEntry*>&ses) 
{
    params->getSyms(ses);
    param->getSyms(ses);
}

void FuncFParam::genCode() 
{
    SymbolEntry *addr_se = new TemporarySymbolEntry(PointerType::get(symbolEntry->getType()), SymbolTable::getLabel());
    Operand *addr = new Operand(addr_se);
    Operand *src = new Operand(symbolEntry);
    dynamic_cast<IdentifierSymbolEntry*>(symbolEntry)->setAddr(addr);
    BasicBlock *bb = builder->getInsertBB()->getParent()->getEntry();
    Instruction *alloca = new AllocaInstruction(addr, symbolEntry);
    bb->insertFront(alloca);
    new StoreInstruction(addr, src, bb);
}

void ReturnStmt::genCode()
{
    BasicBlock *bb = builder->getInsertBB();
    Function *func = bb->getParent();
    BasicBlock *next_bb = new BasicBlock(func);
    Operand *src;
    src = nullptr;
    if(retValue)
    {
        retValue->genCode();
        src = retValue->getOperand();
    }
    new RetInstruction(src, bb);

    builder->setInsertBB(next_bb);
}

void AssignStmt::genCode()
{
    BasicBlock *bb = builder->getInsertBB();
    expr->genCode();
    Operand *addr = dynamic_cast<IdentifierSymbolEntry*>(lval->getSymPtr())->getAddr();
    Operand *src = expr->getOperand();
    /***
     * We haven't implemented array yet, the lval can only be ID. So we just store the result of the `expr` to the addr of the id.
     * If you want to implement array, you have to caculate the address first and then store the result into it.
     */
    new StoreInstruction(addr, src, bb);
}

void BreakStmt::genCode()
{
    new UncondBrInstruction(builder -> getInsertBB() -> pred[0] -> succ[0], builder -> getInsertBB());
}

void ContinueStmt::genCode()
{
    new UncondBrInstruction(builder -> getInsertBB() -> succ[0], builder -> getInsertBB());

}

void Ast::typeCheck()
{
    if(root != nullptr)
        root->typeCheck();
}

void FunctionDef::typeCheck()
{
    if(params)
        params->typeCheck();
    stmt->typeCheck();
}

void BinaryExpr::typeCheck()
{
    // Todo
    expr1->typeCheck();
    expr2->typeCheck();
    Type *type1 = expr1->getSymPtr()->getType();
    Type *type2 = expr2->getSymPtr()->getType();
    if (op == AND || op == OR)
    {
        if(!type1->isI1())
        {
            SymbolEntry *se = new ConstantSymbolEntry(type1, 0);
            Constant *zero = new Constant(se);
            expr1 = new BinaryExpr(new TemporarySymbolEntry(IntType::get(1), SymbolTable::getLabel()), BinaryExpr::NE, expr1, zero);
        }
        if(!type2->isI1())
        {
            SymbolEntry *se = new ConstantSymbolEntry(type2, 0);
            Constant *zero = new Constant(se);
            expr2 = new BinaryExpr(new TemporarySymbolEntry(IntType::get(1), SymbolTable::getLabel()), BinaryExpr::NE, expr2, zero);
        }
        symbolEntry->setType(IntType::get(1));
    }
    else if(op >= L && op <= NE)
    {
        if(type1 != type2)
        {
            if(!type1->isInt() || !type2->isInt())
                exit(-1);
            int numBits1 = dynamic_cast<IntType*>(type1)->getNumBits();
            int numBits2 = dynamic_cast<IntType*>(type2)->getNumBits();
            if(numBits1 > numBits2)
                expr2 = new Extend(new TemporarySymbolEntry(type1, SymbolTable::getLabel()), expr2);
            else
                expr1 = new Extend(new TemporarySymbolEntry(type2, SymbolTable::getLabel()), expr1);
        }
        symbolEntry->setType(IntType::get(1));
    }
    else if(op >= ADD && op <= MOD)
    {
        if(type1 != type2)
        {
            if(!type1->isInt() || !type2->isInt())
                exit(-1);
            int numBits1 = dynamic_cast<IntType*>(type1)->getNumBits();
            int numBits2 = dynamic_cast<IntType*>(type2)->getNumBits();
            if(numBits1 > numBits2)
            {
                expr2 = new Extend(new TemporarySymbolEntry(type1, SymbolTable::getLabel()), expr2);
                symbolEntry->setType(type1);
            }
            else
            {
                expr1 = new Extend(new TemporarySymbolEntry(type2, SymbolTable::getLabel()), expr1);
                symbolEntry->setType(type2);
            }
        }
        else
            symbolEntry->setType(type1);
    }
}

void Constant::typeCheck()
{
    // Todo
}

void Id::typeCheck()
{
    // Todo
}

void IfStmt::typeCheck()
{
    cond->typeCheck();
    thenStmt->typeCheck();
    Type *type = cond->getSymPtr()->getType();
    if(!type->isI1())
    {
        SymbolEntry *se = new ConstantSymbolEntry(type, 0);
        Constant *zero = new Constant(se);
        cond = new BinaryExpr(new TemporarySymbolEntry(IntType::get(1), SymbolTable::getLabel()), BinaryExpr::NE, cond, zero);
    }
}

void IfElseStmt::typeCheck()
{
    cond->typeCheck();
    thenStmt->typeCheck();
    elseStmt->typeCheck();
    Type *type = cond->getSymPtr()->getType();
    if(!type->isI1())
    {
        SymbolEntry *se = new ConstantSymbolEntry(type, 0);
        Constant *zero = new Constant(se);
        cond = new BinaryExpr(new TemporarySymbolEntry(IntType::get(1), SymbolTable::getLabel()), BinaryExpr::NE, cond, zero);
    }
}

void CompoundStmt::typeCheck()
{
    stmt->typeCheck();
}

void SeqNode::typeCheck()
{
    stmt1->typeCheck();
    stmt2->typeCheck();
}

void DeclStmt::typeCheck()
{
    IdentifierSymbolEntry *se = dynamic_cast<IdentifierSymbolEntry *>(id->getSymPtr());
    if(se->isGlobal())
    {
        int my_val = 0;
        if(initVal != nullptr)
        {
            int expr_val;
            if(initVal->constantFolding(expr_val))
                my_val = expr_val;
        }
        se->setInitVal(my_val);
    }
}

void ReturnStmt::typeCheck()
{
    // Todo
    if(retValue != nullptr)
        retValue->typeCheck();
}

void AssignStmt::typeCheck()
{
    // Todo
    lval->typeCheck();
    expr->typeCheck();
}

void BinaryExpr::output(int level)
{
    std::string op_str;
    switch(op)
    {
        case ADD:
            op_str = "add";
            break;
        case SUB:
            op_str = "sub";
            break;
        case AND:
            op_str = "and";
            break;
        case OR:
            op_str = "or";
            break;
        case L:
            op_str = "less";
            break;
    }
    fprintf(yyout, "%*cBinaryExpr\top: %s\n", level, ' ', op_str.c_str());
    expr1->output(level + 4);
    expr2->output(level + 4);
}

void Ast::output()
{
    fprintf(yyout, "program\n");
    if(root != nullptr)
        root->output(4);
}

void Constant::output(int level)
{
    std::string type, value;
    type = symbolEntry->getType()->toStr();
    value = symbolEntry->toStr();
    fprintf(yyout, "%*cIntegerLiteral\tvalue: %s\ttype: %s\n", level, ' ',
            value.c_str(), type.c_str());
}

void Id::output(int level)
{
    std::string name, type;
    int scope;
    name = symbolEntry->toStr();
    type = symbolEntry->getType()->toStr();
    scope = dynamic_cast<IdentifierSymbolEntry*>(symbolEntry)->getScope();
    fprintf(yyout, "%*cId\tname: %s\tscope: %d\ttype: %s\n", level, ' ',
            name.c_str(), scope, type.c_str());
}

void CompoundStmt::output(int level)
{
    fprintf(yyout, "%*cCompoundStmt\n", level, ' ');
    stmt->output(level + 4);
}

void SeqNode::output(int level)
{
    stmt1->output(level);
    stmt2->output(level);
}

void DeclStmt::output(int level)
{
    fprintf(yyout, "%*cDeclStmt\n", level, ' ');
    id->output(level + 4);
}

void IfStmt::output(int level)
{
    fprintf(yyout, "%*cIfStmt\n", level, ' ');
    cond->output(level + 4);
    thenStmt->output(level + 4);
}

void IfElseStmt::output(int level)
{
    fprintf(yyout, "%*cIfElseStmt\n", level, ' ');
    cond->output(level + 4);
    thenStmt->output(level + 4);
    elseStmt->output(level + 4);
}

void ReturnStmt::output(int level)
{
    fprintf(yyout, "%*cReturnStmt\n", level, ' ');
    retValue->output(level + 4);
}

void AssignStmt::output(int level)
{
    fprintf(yyout, "%*cAssignStmt\n", level, ' ');
    lval->output(level + 4);
    expr->output(level + 4);
}

void FunctionDef::output(int level)
{
    std::string name, type;
    name = se->toStr();
    type = se->getType()->toStr();
    fprintf(yyout, "%*cFunctionDefine function name: %s, type: %s\n", level, ' ', 
            name.c_str(), type.c_str());
    stmt->output(level + 4);
}

void BreakStmt::output(int level)
{
    fprintf(yyout, "%*cBreakStmt\n", level, ' ');
}

void ContinueStmt::output(int level)
{
    fprintf(yyout, "%*cContinueStmt\n", level, ' ');
}